// Step 7: a plain "what this is" footer. States clearly that this is a
// reproducible simulation over a mock dataset and simulated nodes, and links
// to the source repository.
export default function Footer() {
  return (
    <footer className="sv-footer" aria-labelledby="footer-h">
      <div className="glass sv-panel">
        <h2 id="footer-h" style={{ fontSize: "1.4rem", marginBottom: "0.8rem" }}>
          What this is
        </h2>
        <p style={{ color: "var(--ink-dim)", maxWidth: "68ch", margin: 0 }}>
          A reproducible, in-browser simulation of the snapvault engine. The
          content-addressed chunking, SHA-256 hashing, deduplicating store,
          incremental snapshots, deterministic replicated placement, and
          parallel verified restore are ported faithfully to TypeScript from the
          C++ storage engine and the Go distribution layer. The dataset is a
          small synthesized mock, the storage nodes are simulated in memory, and
          chunk placement is seeded by content hash, so every run is
          deterministic. Nothing here touches a network or a real cluster.
        </p>
        <p style={{ marginTop: "1rem" }}>
          <a
            className="sv-footer-link"
            href="https://github.com/SAY-5/snapvault"
            target="_blank"
            rel="noreferrer"
          >
            View the source on GitHub
            <span aria-hidden="true"> &nearr;</span>
          </a>
        </p>
        <p className="sv-file-meta" style={{ marginTop: "1.2rem" }}>
          Open your browser console to see the port self-check run the full
          pipeline: dedup, incremental snapshot, distribution, node failure, and
          a byte-for-byte integrity-verified restore.
        </p>
      </div>
    </footer>
  );
}
