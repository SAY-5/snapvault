// Port of core/engine.cpp and core/manifest.cpp: the storage engine ties a
// content store to snapshot manifests. Snapshots are incremental by
// construction: a later snapshot only writes chunks whose hash is new;
// unchanged content is referenced, not re-stored (FORMAT.md).
import { chunkBuffer, DEFAULT_CHUNK_SIZE } from "./chunker";
import { ContentStore } from "./contentStore";
import { sha256Hex } from "./sha256";

// A single file within a snapshot, and its ordered chunk hashes.
export interface FileEntry {
  path: string;
  size: number;
  chunks: string[];
}

// A snapshot manifest (FORMAT.md): snapshots/<name>.json.
export interface Manifest {
  name: string;
  chunkSize: number;
  files: FileEntry[];
}

// A source file as it exists in the mock dataset.
export interface SourceFile {
  path: string;
  data: Uint8Array;
}

export interface SnapshotStats {
  files: number;
  chunks: number; // total chunk references across all files
  newChunks: number; // chunks newly written to the store
  dedupedChunks: number; // chunks skipped because already present
  bytes: number;
}

export interface VerifyResult {
  checked: number;
  bad: number;
  firstBadHash: string;
}

// Per-file detail captured while taking a snapshot, for the UI. Records which
// of a file's chunk references were freshly written vs deduplicated.
export interface FileChunkTrace {
  path: string;
  chunks: { hash: string; isNew: boolean }[];
}

export interface SnapshotResult {
  manifest: Manifest;
  stats: SnapshotStats;
  trace: FileChunkTrace[];
}

export class Engine {
  readonly store = new ContentStore();
  private manifests = new Map<string, Manifest>();
  private chunkSize: number;

  constructor(chunkSize: number = DEFAULT_CHUNK_SIZE) {
    this.chunkSize = chunkSize <= 0 ? DEFAULT_CHUNK_SIZE : chunkSize;
  }

  // Chunk every file, store new chunks (dedup existing), and record a manifest.
  snapshot(name: string, files: SourceFile[]): SnapshotResult {
    const manifest: Manifest = {
      name,
      chunkSize: this.chunkSize,
      files: [],
    };
    const stats: SnapshotStats = {
      files: 0,
      chunks: 0,
      newChunks: 0,
      dedupedChunks: 0,
      bytes: 0,
    };
    const trace: FileChunkTrace[] = [];

    // Order by path, matching the C++ engine's directory walk.
    const ordered = [...files].sort((a, b) => (a.path < b.path ? -1 : 1));

    for (const src of ordered) {
      const fe: FileEntry = { path: src.path, size: src.data.length, chunks: [] };
      stats.bytes += src.data.length;
      const fileTrace: FileChunkTrace = { path: src.path, chunks: [] };

      for (const chunk of chunkBuffer(src.data, this.chunkSize)) {
        fe.chunks.push(chunk.hash);
        stats.chunks++;
        const isNew = this.store.put(chunk.hash, chunk.data);
        if (isNew) stats.newChunks++;
        else stats.dedupedChunks++;
        fileTrace.chunks.push({ hash: chunk.hash, isNew });
      }

      manifest.files.push(fe);
      trace.push(fileTrace);
      stats.files++;
    }

    this.manifests.set(name, manifest);
    return { manifest, stats, trace };
  }

  loadManifest(name: string): Manifest {
    const m = this.manifests.get(name);
    if (!m) throw new Error("snapshot not found: " + name);
    return m;
  }

  // Rebuild every file of snapshot `name`, byte-for-byte, from the store.
  restore(name: string): SourceFile[] {
    const manifest = this.loadManifest(name);
    const out: SourceFile[] = [];
    for (const fe of manifest.files) {
      const parts: Uint8Array[] = [];
      let total = 0;
      for (const hash of fe.chunks) {
        const buf = this.store.get(hash);
        parts.push(buf);
        total += buf.length;
      }
      const data = new Uint8Array(total);
      let off = 0;
      for (const p of parts) {
        data.set(p, off);
        off += p.length;
      }
      out.push({ path: fe.path, data });
    }
    return out;
  }

  // Re-hash every chunk referenced by a snapshot (or all snapshots) and confirm
  // each matches its content-address.
  verify(name = ""): VerifyResult {
    const hashes = new Set<string>();
    const collect = (m: Manifest) => {
      for (const fe of m.files) for (const h of fe.chunks) hashes.add(h);
    };
    if (name === "") {
      for (const m of this.manifests.values()) collect(m);
    } else {
      collect(this.loadManifest(name));
    }

    const result: VerifyResult = { checked: 0, bad: 0, firstBadHash: "" };
    for (const h of hashes) {
      const data = this.store.get(h);
      const actual = sha256Hex(data);
      result.checked++;
      if (actual !== h) {
        result.bad++;
        if (result.firstBadHash === "") result.firstBadHash = h;
      }
    }
    return result;
  }
}

// The sorted, de-duplicated set of chunk hashes referenced by a manifest
// (port of store.Manifest.UniqueChunks).
export function uniqueChunks(m: Manifest): string[] {
  const seen = new Set<string>();
  for (const f of m.files) for (const h of f.chunks) seen.add(h);
  return [...seen].sort();
}
