// Port of internal/cluster/cluster.go: N simulated storage nodes with a
// replication factor R. Placement is a pure function of the chunk hash, node
// count, and R, so runs are fully reproducible (ARCHITECTURE.md failure model).

// ErrChunkUnavailable is thrown when every replica of a chunk is on a node that
// is currently down. Mirrors cluster.ErrChunkUnavailable.
export const ErrChunkUnavailable = "chunk unavailable: all replicas down";

// FNV-1a 64-bit, matching Go's hash/fnv (fnv.New64a). Uses BigInt so the
// mod-N placement is byte-identical to the Go layer.
const FNV_OFFSET_64 = 0xcbf29ce484222325n;
const FNV_PRIME_64 = 0x100000001b3n;
const MASK_64 = 0xffffffffffffffffn;

function fnv1a64(hash: string): bigint {
  let h = FNV_OFFSET_64;
  for (let i = 0; i < hash.length; i++) {
    // Placement hashes the ASCII bytes of the hex string, as in the Go layer.
    h ^= BigInt(hash.charCodeAt(i) & 0xff);
    h = (h * FNV_PRIME_64) & MASK_64;
  }
  return h;
}

export interface NodeStatus {
  id: number;
  up: boolean;
  chunks: number;
}

export class Cluster {
  private up: boolean[];
  private data: Map<string, Uint8Array>[];
  readonly replication: number;

  constructor(n: number, r: number) {
    if (n < 1) throw new Error(`node count must be >= 1, got ${n}`);
    if (r < 1 || r > n)
      throw new Error(`replication factor must be in [1,${n}], got ${r}`);
    this.up = Array.from({ length: n }, () => true);
    this.data = Array.from({ length: n }, () => new Map<string, Uint8Array>());
    this.replication = r;
  }

  nodeCount(): number {
    return this.up.length;
  }

  // The deterministic, sorted list of node ids that hold a replica of the
  // chunk: R consecutive nodes starting from fnv(hash) mod N.
  placement(hash: string): number[] {
    const n = this.up.length;
    const start = Number(fnv1a64(hash) % BigInt(n));
    const ids: number[] = [];
    for (let i = 0; i < this.replication; i++) ids.push((start + i) % n);
    ids.sort((a, b) => a - b);
    return ids;
  }

  // Store a chunk on every node in its placement set, regardless of up/down,
  // mirroring an initial upload.
  put(hash: string, data: Uint8Array): void {
    for (const id of this.placement(hash)) {
      this.data[id].set(hash, data.slice());
    }
  }

  // Fetch a chunk from the first up replica that holds it. Returns the bytes
  // and the serving node id. Throws ErrChunkUnavailable if no up node has it.
  get(hash: string): { data: Uint8Array; node: number } {
    for (const id of this.placement(hash)) {
      if (!this.up[id]) continue;
      const buf = this.data[id].get(hash);
      if (buf) return { data: buf.slice(), node: id };
    }
    throw new Error(ErrChunkUnavailable);
  }

  setUp(id: number, up: boolean): void {
    if (id < 0 || id >= this.up.length) throw new Error(`unknown node id ${id}`);
    this.up[id] = up;
  }

  recoverAll(): void {
    this.up = this.up.map(() => true);
  }

  isUp(id: number): boolean {
    return this.up[id];
  }

  // At least one up node holds the chunk.
  chunkAvailable(hash: string): boolean {
    for (const id of this.placement(hash)) {
      if (this.up[id] && this.data[id].has(hash)) return true;
    }
    return false;
  }

  status(): NodeStatus[] {
    return this.up.map((up, id) => ({ id, up, chunks: this.data[id].size }));
  }
}
