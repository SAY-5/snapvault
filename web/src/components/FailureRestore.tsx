import { useEffect, useRef, useState } from "react";
import { motion, useReducedMotion } from "framer-motion";
import { VaultModel } from "../useVault";
import { Section, Stat, HashChip, Button } from "./primitives";

type Phase = "idle" | "running" | "done" | "failed";

interface FetchRow {
  hash: string;
  node: number;
  available: boolean;
  state: "pending" | "verified" | "unavailable";
}

// Step 6: mark nodes down, then run a parallel verified restore. Chunks are
// pulled concurrently from surviving replicas, each hash-verified on arrival
// (green tick). If any chunk loses all replicas, the restore fails cleanly.
export default function FailureRestore({ vault }: { vault: VaultModel }) {
  const reduce = useReducedMotion();
  const [phase, setPhase] = useState<Phase>("idle");
  const [rows, setRows] = useState<FetchRow[]>([]);
  const timers = useRef<number[]>([]);
  const PARALLEL = 4;

  const counts = vault.nodeChunkCounts();
  const chunksUp = vault.chunksUp();
  const totalChunks = vault.uniqueChunkCount;
  const nodesUp = vault.nodes - vault.down.size;
  const anyLost = chunksUp < totalChunks;

  const clearTimers = () => {
    timers.current.forEach((t) => clearTimeout(t));
    timers.current = [];
  };

  useEffect(() => () => clearTimers(), []);

  // Reset the animation whenever the cluster state changes.
  useEffect(() => {
    clearTimers();
    setPhase("idle");
    setRows([]);
  }, [vault.down]);

  const runRestore = () => {
    clearTimers();
    const plan = vault.plan();
    const initial: FetchRow[] = plan.map((p) => ({
      hash: p.hash,
      node: p.node,
      available: p.available,
      state: "pending",
    }));
    setRows(initial);
    setPhase("running");

    const willFail = plan.some((p) => !p.available);
    // Walk the plan in PARALLEL lanes; each fetch resolves after a short,
    // lane-staggered delay, mirroring concurrent workers.
    const step = reduce ? 0 : 150;
    let failedAt = -1;
    plan.forEach((p, i) => {
      const lane = i % PARALLEL;
      const wave = Math.floor(i / PARALLEL);
      const delay = reduce ? 0 : wave * step + lane * 30;
      const t = window.setTimeout(() => {
        setRows((prev) => {
          const next = [...prev];
          next[i] = {
            ...next[i],
            state: p.available ? "verified" : "unavailable",
          };
          return next;
        });
        if (!p.available && failedAt === -1) failedAt = i;
      }, delay);
      timers.current.push(t);
    });

    const settle = reduce ? 0 : Math.ceil(plan.length / PARALLEL) * step + 200;
    const done = window.setTimeout(() => {
      setPhase(willFail ? "failed" : "done");
    }, settle);
    timers.current.push(done);
  };

  const verified = rows.filter((r) => r.state === "verified").length;

  return (
    <Section
      id="restore"
      eyebrow="failure and parallel restore"
      title="Take a node down. Restore anyway."
      lede={
        <>
          Mark nodes offline, then run a parallel restore. A pool of workers
          fetches chunks concurrently from whichever replica is still up,
          verifies each chunk against its content-address on arrival, and
          reassembles the files. Down every replica of a single chunk and the
          restore aborts cleanly instead of writing a corrupt file.
        </>
      }
    >
      <div className="sv-controls" role="group" aria-label="node failure controls">
        {Array.from({ length: vault.nodes }).map((_, id) => {
          const down = vault.down.has(id);
          return (
            <button
              key={id}
              className={`sv-node-toggle ${down ? "is-down" : "is-up"}`}
              onClick={() => vault.toggleNode(id)}
              aria-pressed={down}
              aria-label={`node ${id} ${down ? "is down, click to bring up" : "is up, click to take down"}`}
            >
              <span
                className={`sv-dot ${down ? "sv-dot-down" : "sv-dot-up"}`}
                aria-hidden="true"
              />
              node {id}
              <span className="sv-file-meta">{counts[id]}c</span>
            </button>
          );
        })}
        <Button variant="ghost" onClick={vault.recoverAll} ariaLabel="recover all nodes">
          Recover all
        </Button>
      </div>

      <div className="glass sv-stats">
        <Stat label="Nodes up" value={`${nodesUp}/${vault.nodes}`} tone={nodesUp < vault.nodes ? "warn" : "accent"} />
        <Stat
          label="Chunks reachable"
          value={`${chunksUp}/${totalChunks}`}
          tone={anyLost ? "danger" : "accent"}
        />
        <Stat label="Verified" value={`${verified}/${totalChunks}`} tone="accent" />
        <Stat label="Parallelism" value={PARALLEL} tone="steel" />
      </div>

      <div className="sv-controls" style={{ marginTop: "1.4rem" }}>
        <Button
          variant="primary"
          onClick={runRestore}
          disabled={phase === "running"}
          ariaLabel="run parallel restore"
        >
          {phase === "running" ? "Restoring..." : "Run parallel restore"}
        </Button>
        {anyLost && (
          <span className="sv-file-meta" style={{ color: "var(--danger)" }}>
            {totalChunks - chunksUp} chunk(s) have lost every replica: restore
            will fail cleanly
          </span>
        )}
      </div>

      {rows.length > 0 && (
        <>
          <div className="sv-progress" style={{ margin: "1.2rem 0 0.8rem" }} aria-hidden="true">
            <motion.div
              className="sv-progress-fill"
              animate={{ width: `${(verified / totalChunks) * 100}%` }}
              transition={{ duration: reduce ? 0 : 0.3 }}
            />
          </div>
          <div className="glass sv-log" role="log" aria-label="restore fetch log">
            {rows.map((r, i) => (
              <div key={`${r.hash}-${i}`} className="sv-log-line">
                {r.state === "verified" ? (
                  <span className="sv-tick" aria-hidden="true">
                    &#10003;
                  </span>
                ) : r.state === "unavailable" ? (
                  <span className="sv-cross" aria-hidden="true">
                    &#10007;
                  </span>
                ) : (
                  <span style={{ color: "var(--ink-faint)" }} aria-hidden="true">
                    &middot;
                  </span>
                )}
                <HashChip
                  hash={r.hash}
                  tone={r.state === "unavailable" ? "danger" : "accent"}
                />
                <span>
                  {r.state === "pending" && "fetching..."}
                  {r.state === "verified" && `fetched from node ${r.node}, hash verified`}
                  {r.state === "unavailable" &&
                    "all replicas down: chunk unavailable"}
                </span>
              </div>
            ))}
          </div>
        </>
      )}

      {phase === "done" && (
        <div className="sv-banner sv-banner-good" style={{ marginTop: "1.2rem" }}>
          <span aria-hidden="true">&#10003;</span>
          Node failure survived. All {totalChunks} chunks fetched from surviving
          replicas, every hash verified, and the restored tree matches the
          original byte-for-byte{" "}
          {vault.restoreValid() ? "(integrity confirmed)" : ""}.
        </div>
      )}
      {phase === "failed" && (
        <div className="sv-banner sv-banner-bad" style={{ marginTop: "1.2rem" }}>
          <span aria-hidden="true">&#10007;</span>
          Restore aborted cleanly: at least one chunk lost every replica
          (ErrChunkUnavailable). No corrupt file is written. Bring a node back up
          and restore again.
        </div>
      )}
    </Section>
  );
}
