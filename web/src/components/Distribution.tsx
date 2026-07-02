import { useState } from "react";
import { motion, useReducedMotion } from "framer-motion";
import { VaultModel } from "../useVault";
import { Section, Stat, HashChip } from "./primitives";

// Step 5: a grid of N nodes; each unique chunk of v2 is replicated to R nodes.
// Selecting a chunk highlights its deterministic replica set. Placement is a
// pure function of the hash, so the same chunk always lands on the same nodes.
export default function Distribution({ vault }: { vault: VaultModel }) {
  const reduce = useReducedMotion();
  const hashes = vault.uniqueHashes;
  const [selected, setSelected] = useState<string>(hashes[0] ?? "");
  const placement = selected ? vault.placementOf(selected) : [];
  const counts = vault.nodeChunkCounts();

  return (
    <Section
      id="distribution"
      eyebrow="replicated placement"
      title="Spread every chunk across the cluster."
      lede={
        <>
          The distribution layer places each unique chunk on{" "}
          <strong style={{ color: "var(--accent-bright)" }}>{vault.replicas}</strong> of{" "}
          <strong style={{ color: "var(--accent-bright)" }}>{vault.nodes}</strong> nodes.
          The replica set is chosen deterministically from the chunk hash, so a
          run is fully reproducible with no random or wall-clock state.
        </>
      }
    >
      <div className="glass sv-stats">
        <Stat label="Nodes" value={vault.nodes} />
        <Stat label="Replication" value={`${vault.replicas}x`} tone="accent" />
        <Stat label="Unique chunks" value={vault.uniqueChunkCount} />
        <Stat
          label="Chunk copies"
          value={vault.uniqueChunkCount * vault.replicas}
          tone="steel"
        />
      </div>

      <p className="sv-file-meta" style={{ margin: "1.6rem 0 0.7rem" }}>
        Pick a chunk to trace its replica set:
      </p>
      <div className="sv-chunkrow" role="listbox" aria-label="chunk selector">
        {hashes.map((h) => (
          <button
            key={h}
            role="option"
            aria-selected={h === selected}
            className={`sv-chunk ${h === selected ? "sv-chunk-new" : "sv-chunk-dedup"}`}
            onClick={() => setSelected(h)}
            title={h}
            style={{ cursor: "pointer" }}
          >
            {h.slice(0, 6)}
          </button>
        ))}
      </div>

      <p className="sv-file-meta" style={{ margin: "1.4rem 0 0.7rem" }}>
        Chunk <HashChip hash={selected} /> lives on nodes{" "}
        <strong style={{ color: "var(--accent-bright)" }}>
          {placement.map((p) => `n${p}`).join(", ")}
        </strong>
      </p>

      <div className="sv-nodegrid">
        {Array.from({ length: vault.nodes }).map((_, id) => {
          const holds = placement.includes(id);
          return (
            <motion.div
              key={id}
              className={`sv-node sv-node-up ${holds ? "sv-node-serving" : ""}`}
              animate={
                reduce || !holds
                  ? {}
                  : { boxShadow: ["var(--glow-steel)", "var(--glow-accent)", "var(--glow-steel)"] }
              }
              transition={{ duration: 1.6, repeat: Infinity }}
            >
              <div className="sv-node-title">
                <span className="sv-node-name">node {id}</span>
                <span className="sv-dot sv-dot-up" aria-hidden="true" />
              </div>
              <div className="sv-file-meta" style={{ marginBottom: "0.5rem" }}>
                {counts[id]} chunks {holds ? "(holds selected)" : ""}
              </div>
              <div className="sv-node-chunks" aria-hidden="true">
                {Array.from({ length: counts[id] }).map((__, k) => (
                  <span
                    key={k}
                    className={`sv-nodechunk ${holds && k === 0 ? "sv-nodechunk-hot" : ""}`}
                  />
                ))}
              </div>
            </motion.div>
          );
        })}
      </div>

      <div className="glass sv-panel" style={{ marginTop: "1.4rem" }}>
        <p style={{ margin: 0, color: "var(--ink-dim)", fontSize: "0.92rem" }}>
          Replication health:{" "}
          <strong style={{ color: "var(--accent-bright)" }}>
            {vault.nodes}/{vault.nodes} nodes up
          </strong>
          . Every chunk has {vault.replicas} copies, so it can survive up to{" "}
          {vault.replicas - 1} node failures. The next section takes a node down
          and restores anyway.
        </p>
      </div>
    </Section>
  );
}
