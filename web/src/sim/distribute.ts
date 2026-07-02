// Port of internal/distribute/distribute.go: put a snapshot's chunks onto the
// cluster with replication, and restore them in parallel, verifying each
// chunk's content-address on arrival. A mismatch or unavailable chunk aborts
// the whole restore (the Go code cancels a shared context on first error).
import { Cluster, ErrChunkUnavailable } from "./cluster";
import { Engine, Manifest, uniqueChunks } from "./engine";
import { sha256Hex } from "./sha256";

// Upload every unique chunk of the manifest to the cluster, replicating per the
// placement policy. Returns the number of unique chunks distributed.
export function distribute(engine: Engine, c: Cluster, m: Manifest): number {
  const chunks = uniqueChunks(m);
  for (const h of chunks) {
    c.put(h, engine.store.get(h));
  }
  return chunks.length;
}

export interface RestoreStats {
  files: number;
  chunks: number; // unique chunks fetched
  verified: number; // chunks that passed hash verification
  parallelism: number;
}

// A single fetch event, for the UI to animate the parallel restore.
export interface FetchEvent {
  hash: string;
  node: number; // serving node id, or -1 on failure
  ok: boolean;
  error?: string;
}

// Synchronous restore used by the console self-check and as the model behind
// the animated restore. Fetches each unique chunk, verifies its hash, and
// aborts on the first failure, exactly like the Go worker pool plus shared
// cancel context (the observable outcome is identical for a pure simulation).
export function restore(
  c: Cluster,
  m: Manifest,
  parallel: number
): {
  stats: RestoreStats;
  events: FetchEvent[];
  ok: boolean;
  error?: string;
} {
  if (parallel < 1) parallel = 1;
  const unique = uniqueChunks(m);
  const stats: RestoreStats = {
    files: 0,
    chunks: unique.length,
    verified: 0,
    parallelism: parallel,
  };
  const events: FetchEvent[] = [];

  for (const h of unique) {
    try {
      const { data, node } = c.get(h);
      const got = sha256Hex(data);
      if (got !== h) {
        events.push({
          hash: h,
          node,
          ok: false,
          error: `chunk ${h} failed integrity check (got ${got})`,
        });
        return { stats, events, ok: false, error: events[events.length - 1].error };
      }
      stats.verified++;
      events.push({ hash: h, node, ok: true });
    } catch {
      events.push({ hash: h, node: -1, ok: false, error: ErrChunkUnavailable });
      return { stats, events, ok: false, error: ErrChunkUnavailable };
    }
  }

  stats.files = m.files.length;
  return { stats, events, ok: true };
}

// Pre-compute the ordered fetch plan (which node will serve each unique chunk,
// or whether it is unavailable) without mutating state. The animated restore
// walks this plan with `parallel` concurrent lanes.
export interface PlannedFetch {
  hash: string;
  node: number; // serving node id, or -1 if unavailable
  available: boolean;
}

export function planRestore(c: Cluster, m: Manifest): PlannedFetch[] {
  return uniqueChunks(m).map((hash) => {
    try {
      // Serving node is the first up replica that holds the chunk, exactly as
      // Cluster.get resolves it.
      const { node } = c.get(hash);
      return { hash, node, available: true };
    } catch {
      return { hash, node: -1, available: false };
    }
  });
}
