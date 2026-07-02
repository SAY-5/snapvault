import { useState } from "react";
import { AnimatePresence, motion, useReducedMotion } from "framer-motion";
import { VaultModel } from "../useVault";
import { SnapshotResult } from "../sim/engine";
import { Section, Stat, HashChip, Button } from "./primitives";

// Step 4: files split into content-hashed chunks; identical chunks collapse to
// one stored copy. Then an incremental snapshot writes only the changed chunk.
export default function ChunksDedup({ vault }: { vault: VaultModel }) {
  const [snap, setSnap] = useState<"v1" | "v2">("v1");
  const current: SnapshotResult = snap === "v1" ? vault.v1 : vault.v2;
  const reduce = useReducedMotion();

  const dedupPct = Math.round(
    (current.stats.dedupedChunks / current.stats.chunks) * 100
  );

  return (
    <Section
      id="chunks"
      eyebrow="content-addressed storage"
      title="Chunk it. Hash it. Store each byte-sequence once."
      lede={
        <>
          Every file is split into fixed-size chunks, and each chunk is named by
          the SHA-256 of its own bytes. Storing a chunk whose hash already exists
          is a no-op, so identical files and identical regions share one stored
          copy. That is deduplication, for free.
        </>
      }
    >
      <div className="sv-controls" role="group" aria-label="snapshot selector">
        <Button
          variant={snap === "v1" ? "primary" : "ghost"}
          onClick={() => setSnap("v1")}
          ariaLabel="show first snapshot"
        >
          Snapshot v1
        </Button>
        <Button
          variant={snap === "v2" ? "primary" : "ghost"}
          onClick={() => setSnap("v2")}
          ariaLabel="show incremental snapshot"
        >
          Snapshot v2 (edit one file)
        </Button>
        <span className="sv-file-meta">
          {snap === "v1"
            ? "logs/app.log.bak is a byte-for-byte copy of logs/app.log"
            : "config/settings.ini was edited; only its changed chunk is new"}
        </span>
      </div>

      <div className="glass sv-stats" aria-live="polite">
        <Stat label="Files" value={current.stats.files} />
        <Stat label="Chunk refs" value={current.stats.chunks} />
        <Stat label="New chunks" value={current.stats.newChunks} tone="accent" />
        <Stat
          label="Deduplicated"
          value={current.stats.dedupedChunks}
          tone="steel"
        />
        <Stat label="Dedup ratio" value={`${dedupPct}%`} tone="accent" />
      </div>

      <p className="sv-file-meta" style={{ margin: "1.4rem 0 0.6rem" }}>
        <span className="sv-legend sv-legend-new">newly stored</span>
        <span className="sv-legend sv-legend-dedup">deduplicated (already stored)</span>
      </p>

      <div className="sv-files">
        <AnimatePresence mode="wait">
          <motion.div
            key={snap}
            className="sv-files"
            initial={reduce ? false : { opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            exit={reduce ? undefined : { opacity: 0, y: -8 }}
            transition={{ duration: 0.35 }}
          >
            {current.trace.map((ft) => {
              const file = current.manifest.files.find((f) => f.path === ft.path);
              return (
                <div key={ft.path} className="glass sv-file">
                  <div className="sv-file-head">
                    <span className="sv-file-path mono">{ft.path}</span>
                    <span className="sv-file-meta">
                      {file?.size} bytes &middot; {ft.chunks.length} chunks
                    </span>
                  </div>
                  <div className="sv-chunkrow">
                    {ft.chunks.map((c, i) => (
                      <motion.span
                        key={`${c.hash}-${i}`}
                        className={`sv-chunk ${c.isNew ? "sv-chunk-new" : "sv-chunk-dedup"}`}
                        title={`${c.hash} (${c.isNew ? "newly stored" : "deduplicated"})`}
                        initial={reduce ? false : { opacity: 0, scale: 0.6 }}
                        animate={{ opacity: 1, scale: 1 }}
                        transition={{ duration: 0.25, delay: reduce ? 0 : i * 0.012 }}
                      >
                        {c.hash.slice(0, 6)}
                      </motion.span>
                    ))}
                  </div>
                </div>
              );
            })}
          </motion.div>
        </AnimatePresence>
      </div>

      <div className="glass sv-panel" style={{ marginTop: "1.4rem" }}>
        <p style={{ margin: 0, color: "var(--ink-dim)", fontSize: "0.92rem" }}>
          {snap === "v1" ? (
            <>
              The first snapshot stores{" "}
              <strong style={{ color: "var(--accent-bright)" }}>
                {current.stats.newChunks}
              </strong>{" "}
              unique chunks and skips{" "}
              <strong style={{ color: "var(--steel-bright)" }}>
                {current.stats.dedupedChunks}
              </strong>{" "}
              that already match a stored content-address, the entire duplicate
              file among them.
            </>
          ) : (
            <>
              The incremental snapshot references the same content, so only{" "}
              <strong style={{ color: "var(--accent-bright)" }}>
                {current.stats.newChunks}
              </strong>{" "}
              chunk is written for the one edited file (
              <HashChip
                hash={
                  vault.v2.trace
                    .find((t) => t.path === "config/settings.ini")
                    ?.chunks.find((c) => c.isNew)?.hash ?? ""
                }
                tone="accent"
              />
              ). Everything else is deduplicated.
            </>
          )}
        </p>
      </div>
    </Section>
  );
}
