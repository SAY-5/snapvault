import { useEffect } from "react";
import { runSelfCheck } from "./sim/selfcheck";

export default function App() {
  useEffect(() => {
    const r = runSelfCheck();
    // Faithful-port self-check, mirroring scripts/demo.sh.
    console.groupCollapsed("%csnapvault sim self-check", "color:#35d0b5;font-weight:700");
    for (const l of r.lines) console.log(l);
    console.log(
      "dedup:",
      r.dedupOk,
      "| incremental:",
      r.incrementalOk,
      "| survived node failure:",
      r.survivedFailureOk,
      "| clean failure:",
      r.cleanFailureOk
    );
    console.groupEnd();
  }, []);

  return (
    <main style={{ padding: "4rem", textAlign: "center" }}>
      <h1 style={{ fontSize: "3rem" }}>snapvault</h1>
      <p style={{ color: "var(--ink-dim)" }}>
        Back up once. Restore through a node failure.
      </p>
      <p className="mono" style={{ color: "var(--ink-faint)", fontSize: "0.85rem" }}>
        open the console for the port self-check
      </p>
    </main>
  );
}
