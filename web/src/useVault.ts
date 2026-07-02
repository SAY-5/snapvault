import { useMemo, useState, useCallback } from "react";
import { Engine, SnapshotResult, SourceFile, uniqueChunks } from "./sim/engine";
import { Cluster } from "./sim/cluster";
import { distribute, planRestore, PlannedFetch } from "./sim/distribute";
import { makeDataset, editOneFile, treesEqual } from "./sim/dataset";

const NODES = 5;
const REPLICAS = 3;

// Build the whole pipeline once: v1 snapshot (dedup), v2 incremental snapshot,
// and a cluster with v2 distributed. Returns a fresh, reproducible model.
function buildModel() {
  const engine = new Engine();
  const v1files = makeDataset();
  const v1 = engine.snapshot("v1", v1files);
  const v2files = editOneFile(v1files);
  const v2 = engine.snapshot("v2", v2files);

  const cluster = new Cluster(NODES, REPLICAS);
  const unique = distribute(engine, cluster, v2.manifest);

  return {
    engine,
    v1,
    v2,
    v1files,
    v2files,
    cluster,
    unique,
    nodes: NODES,
    replicas: REPLICAS,
  };
}

export interface VaultModel {
  v1: SnapshotResult;
  v2: SnapshotResult;
  v1files: SourceFile[];
  v2files: SourceFile[];
  uniqueChunkCount: number;
  nodes: number;
  replicas: number;
  // Node up/down state (a slice of cluster status kept in React state).
  down: Set<number>;
  toggleNode: (id: number) => void;
  recoverAll: () => void;
  // Placement + status derived from current down-set.
  placementOf: (hash: string) => number[];
  nodeChunkCounts: () => number[];
  chunksUp: () => number;
  // The ordered restore plan against the current cluster state.
  plan: () => PlannedFetch[];
  // Integrity: does a full restore reproduce v2 byte-for-byte right now?
  restoreValid: () => boolean;
  uniqueHashes: string[];
}

export function useVault(): VaultModel {
  const model = useMemo(buildModel, []);
  const [down, setDown] = useState<Set<number>>(() => new Set());

  // Apply the down-set to the (single) cluster instance before any query.
  const sync = useCallback(() => {
    for (let id = 0; id < model.cluster.nodeCount(); id++) {
      model.cluster.setUp(id, !down.has(id));
    }
  }, [down, model.cluster]);

  const toggleNode = useCallback((id: number) => {
    setDown((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);

  const recoverAll = useCallback(() => setDown(new Set()), []);

  const uniqueHashes = useMemo(
    () => uniqueChunks(model.v2.manifest),
    [model.v2.manifest]
  );

  const placementOf = useCallback(
    (hash: string) => model.cluster.placement(hash),
    [model.cluster]
  );

  const nodeChunkCounts = useCallback(() => {
    sync();
    return model.cluster.status().map((s) => s.chunks);
  }, [model.cluster, sync]);

  const chunksUp = useCallback(() => {
    sync();
    return uniqueHashes.filter((h) => model.cluster.chunkAvailable(h)).length;
  }, [model.cluster, uniqueHashes, sync]);

  const plan = useCallback(() => {
    sync();
    return planRestore(model.cluster, model.v2.manifest);
  }, [model.cluster, model.v2.manifest, sync]);

  const restoreValid = useCallback(() => {
    sync();
    const p = planRestore(model.cluster, model.v2.manifest);
    if (p.some((f) => !f.available)) return false;
    // Every chunk fetchable and hash-verified; the engine reassembles v2.
    const restored = model.engine.restore("v2");
    return treesEqual(model.v2files, restored);
  }, [model.cluster, model.engine, model.v2.manifest, model.v2files, sync]);

  return {
    v1: model.v1,
    v2: model.v2,
    v1files: model.v1files,
    v2files: model.v2files,
    uniqueChunkCount: model.unique,
    nodes: model.nodes,
    replicas: model.replicas,
    down,
    toggleNode,
    recoverAll,
    placementOf,
    nodeChunkCounts,
    chunksUp,
    plan,
    restoreValid,
    uniqueHashes,
  };
}
