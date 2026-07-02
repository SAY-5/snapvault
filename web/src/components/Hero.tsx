import { motion, useReducedMotion } from "framer-motion";
import { VaultModel } from "../useVault";

// The dominant hero visual: a slow-rotating "vault ring" of nodes with chunks
// orbiting inward, over the headline and the load-bearing stat.
export default function Hero({ vault }: { vault: VaultModel }) {
  const reduce = useReducedMotion();
  const dedupPct = Math.round(
    (vault.v1.stats.dedupedChunks / vault.v1.stats.chunks) * 100
  );

  return (
    <header className="sv-hero" aria-labelledby="hero-h">
      <div className="sv-hero-copy">
        <span className="sv-eyebrow mono">distributed backup / cold-storage</span>
        <h1 id="hero-h" className="sv-hero-title">
          Back up once.
          <br />
          Restore through a<br />
          <span className="sv-accent-text">node failure.</span>
        </h1>
        <p className="sv-hero-lede">
          snapvault chunks a dataset by content, stores each unique chunk exactly
          once, replicates it across storage nodes, and restores in parallel,
          verifying every chunk by hash and surviving a node going dark. This is
          the real engine, ported to run in your browser.
        </p>
        <div className="sv-hero-stat">
          <motion.span
            className="sv-hero-bignum mono"
            initial={reduce ? false : { opacity: 0, scale: 0.9 }}
            animate={{ opacity: 1, scale: 1 }}
            transition={{ duration: 0.7, ease: "easeOut" }}
          >
            {vault.replicas}&times; replicated
          </motion.span>
          <span className="sv-hero-stat-sub">
            every chunk survives up to {vault.replicas - 1} node failures &middot;{" "}
            {dedupPct}% of the first snapshot deduplicated
          </span>
        </div>
        <a className="sv-hero-cta" href="#chunks">
          See how it works
          <span aria-hidden="true"> &darr;</span>
        </a>
      </div>

      <div className="sv-hero-visual" aria-hidden="true">
        <VaultRing count={vault.nodes} reduce={!!reduce} />
      </div>
    </header>
  );
}

function VaultRing({ count, reduce }: { count: number; reduce: boolean }) {
  const R = 128;
  const center = 170;
  return (
    <div className="sv-ring-wrap">
      <motion.div
        className="sv-ring"
        animate={reduce ? {} : { rotate: 360 }}
        transition={{ duration: 60, ease: "linear", repeat: Infinity }}
      >
        {Array.from({ length: count }).map((_, i) => {
          const a = (i / count) * Math.PI * 2 - Math.PI / 2;
          const x = center + R * Math.cos(a);
          const y = center + R * Math.sin(a);
          return (
            <motion.div
              key={i}
              className="sv-ring-node"
              style={{ left: x, top: y }}
              animate={reduce ? {} : { rotate: -360 }}
              transition={{ duration: 60, ease: "linear", repeat: Infinity }}
            >
              <span className="mono">n{i}</span>
            </motion.div>
          );
        })}
      </motion.div>
      <div className="sv-ring-core">
        <div className="sv-ring-core-inner mono">SHA-256</div>
      </div>
      {!reduce &&
        Array.from({ length: 5 }).map((_, i) => (
          <motion.div
            key={i}
            className="sv-ring-pulse"
            initial={{ scale: 0.5, opacity: 0.5 }}
            animate={{ scale: 1.9, opacity: 0 }}
            transition={{
              duration: 3.4,
              ease: "easeOut",
              repeat: Infinity,
              delay: i * 0.68,
            }}
          />
        ))}
    </div>
  );
}
