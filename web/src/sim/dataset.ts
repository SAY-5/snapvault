// A small mock dataset, mirroring scripts/demo.sh: a log file with an identical
// copy (to show dedup), plus small config files. Sizes are scaled down from the
// C++ demo so the whole dataset splits into a legible number of on-screen
// chunks while exercising the identical code path.
import { SourceFile } from "./engine";

const enc = new TextEncoder();

function logBody(lines: number): string {
  let s = "";
  for (let i = 1; i <= lines; i++) s += `record ${i} payload data line\n`;
  return s;
}

// Build the initial dataset. logs/app.log and logs/app.log.bak are byte-for-byte
// identical, so every chunk of the copy deduplicates against the original.
export function makeDataset(): SourceFile[] {
  const appLog = logBody(24);
  return [
    { path: "logs/app.log", data: enc.encode(appLog) },
    { path: "logs/app.log.bak", data: enc.encode(appLog) }, // identical -> deduped
    {
      path: "config/settings.ini",
      data: enc.encode("name = snapvault\nnodes = 5\nreplicas = 3\n"),
    },
    { path: "config/blob.dat", data: enc.encode("Z".repeat(200)) },
  ];
}

// Edit exactly one file, mirroring demo.sh step 3. Only the tail chunk of
// settings.ini changes, so an incremental snapshot writes a single new chunk.
export function editOneFile(files: SourceFile[]): SourceFile[] {
  return files.map((f) =>
    f.path === "config/settings.ini"
      ? {
          path: f.path,
          data: enc.encode(
            "name = snapvault\nnodes = 7\nreplicas = 3\nchanged = true\n"
          ),
        }
      : f
  );
}

// Compare two file trees byte-for-byte (the demo's final integrity check).
export function treesEqual(a: SourceFile[], b: SourceFile[]): boolean {
  if (a.length !== b.length) return false;
  const byPath = new Map(b.map((f) => [f.path, f.data]));
  for (const f of a) {
    const other = byPath.get(f.path);
    if (!other || other.length !== f.data.length) return false;
    for (let i = 0; i < f.data.length; i++)
      if (f.data[i] !== other[i]) return false;
  }
  return true;
}
