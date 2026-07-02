// A console self-check that runs the full pipeline in the browser, mirroring
// scripts/demo.sh: first snapshot (dedup), incremental snapshot (one new
// chunk), distribute with replication, fail a node, parallel restore with
// verification, and a byte-for-byte integrity check of the restored tree.
import { Cluster } from "./cluster";
import { makeDataset, editOneFile, treesEqual } from "./dataset";
import { distribute, restore } from "./distribute";
import { Engine, uniqueChunks } from "./engine";

export interface SelfCheckReport {
  dedupOk: boolean;
  incrementalOk: boolean;
  survivedFailureOk: boolean;
  cleanFailureOk: boolean;
  lines: string[];
}

export function runSelfCheck(): SelfCheckReport {
  const lines: string[] = [];
  const log = (s: string) => lines.push(s);

  const engine = new Engine();

  // 1 + 2. First snapshot: the identical .bak file should fully deduplicate.
  const v1 = makeDataset();
  const s1 = engine.snapshot("v1", v1);
  const dedupOk = s1.stats.dedupedChunks > 0 && s1.stats.newChunks < s1.stats.chunks;
  log(
    `snapshot v1: files=${s1.stats.files} chunkRefs=${s1.stats.chunks} ` +
      `new=${s1.stats.newChunks} deduped=${s1.stats.dedupedChunks}`
  );

  // 3. Incremental snapshot after editing one file: only new chunks written.
  const v2files = editOneFile(v1);
  const s2 = engine.snapshot("v2", v2files);
  const incrementalOk = s2.stats.newChunks >= 1 && s2.stats.newChunks < s2.stats.chunks;
  log(
    `snapshot v2 (incremental): chunkRefs=${s2.stats.chunks} ` +
      `new=${s2.stats.newChunks} deduped=${s2.stats.dedupedChunks}`
  );

  // 4. Distribute v2 across 5 nodes, replication 3.
  const cluster = new Cluster(5, 3);
  const unique = distribute(engine, cluster, s2.manifest);
  log(
    `distributed v2: nodes=${cluster.nodeCount()} replicas=${cluster.replication} ` +
      `uniqueChunks=${unique} copies=${unique * cluster.replication}`
  );

  // 5. Fail one node, then parallel-restore with verification.
  cluster.setUp(2, false);
  const r = restore(cluster, s2.manifest, 4);
  const restored = r.ok ? engine.restore("v2") : [];
  const survivedFailureOk =
    r.ok && r.stats.verified === r.stats.chunks && treesEqual(v2files, restored);
  log(
    `node 2 down; restore v2: verified=${r.stats.verified}/${r.stats.chunks} ` +
      `parallel=${r.stats.parallelism} ok=${r.ok} treeMatch=${survivedFailureOk}`
  );

  // 6. Down all replicas of one chunk: the restore must fail cleanly.
  const anyChunk = uniqueChunks(s2.manifest)[0];
  const c2 = new Cluster(5, 3);
  distribute(engine, c2, s2.manifest);
  for (const id of c2.placement(anyChunk)) c2.setUp(id, false);
  const rf = restore(c2, s2.manifest, 4);
  const cleanFailureOk = !rf.ok && rf.error === "chunk unavailable: all replicas down";
  log(
    `all replicas of ${anyChunk.slice(0, 7)} down; restore failed cleanly: ` +
      `${cleanFailureOk} (${rf.error ?? "no error"})`
  );

  return { dedupOk, incrementalOk, survivedFailureOk, cleanFailureOk, lines };
}
