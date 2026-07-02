import { useEffect } from "react";
import { runSelfCheck } from "./sim/selfcheck";
import { useVault } from "./useVault";
import Hero from "./components/Hero";
import ChunksDedup from "./components/ChunksDedup";
import Distribution from "./components/Distribution";
import FailureRestore from "./components/FailureRestore";
import "./app.css";

export default function App() {
  const vault = useVault();

  useEffect(() => {
    const r = runSelfCheck();
    console.groupCollapsed(
      "%csnapvault sim self-check",
      "color:#35d0b5;font-weight:700"
    );
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
    <div className="sv-app">
      <Hero vault={vault} />
      <ChunksDedup vault={vault} />
      <Distribution vault={vault} />
      <FailureRestore vault={vault} />
    </div>
  );
}
