# snapvault

A distributed backup and rapid-restore service. It takes incremental snapshots
of a dataset using content-addressed storage, replicates the resulting chunks
across simulated storage nodes, and restores them in parallel while verifying
integrity through hashing and recovering across node failures.

The project is deliberately split across two languages that share one on-disk
format:

- **C++17 engine (`core/`, `svcore`)** owns content-addressed storage: a
  from-scratch SHA-256, a chunker, a deduplicating content store, and snapshot
  manifests. This is where snapshots, restores, and verification happen.
- **Go engine (`cmd/snapvault`, `internal/*`)** owns the distributed layer: N
  simulated storage nodes, replication factor R, deterministic placement,
  parallel verified restore, and node-failure recovery.

Everything runs on one machine. The dataset is synthesized on the fly, the
storage nodes are simulated in memory, and chunk placement is seeded by content
hash, so runs are fully reproducible with no real cluster required. The shared format is the
single source of truth and lives in [FORMAT.md](FORMAT.md); the design is
described in [ARCHITECTURE.md](ARCHITECTURE.md).

## Quick start

```bash
make build     # build the C++ engine and the Go binary
make test      # run C++ CTest suites and Go race tests
make demo      # full pipeline on a synthesized mock dataset
```

## The pieces

### C++ storage engine: `svcore`

```
svcore snapshot <name> <dir> [--store DIR] [--chunk N]
svcore restore  <name> <dir> [--store DIR]
svcore verify   [name]       [--store DIR]
svcore drop     <name>       [--store DIR]
svcore gc       [--dry-run]  [--store DIR]
svcore diff     <snapA> <snapB> [--store DIR]
svcore retain   --keep-last N   [--store DIR]
```

`snapshot` chunks every file, stores new chunks (deduplicating existing ones),
and writes a manifest. A second snapshot only stores chunks whose content is
new. `verify` re-hashes every stored chunk against its content-address.
`drop` deletes a snapshot's manifest, and `gc` reclaims chunks that no
remaining manifest references (`--dry-run` lists the candidates first);
chunks still shared with other snapshots survive. `diff` compares two
snapshots (files added, removed, changed, plus chunk-level stats), and
`retain` keeps the newest N snapshots, drops the rest, and gc's the store.

### Go distribution layer: `snapvault`

```
snapvault put      <snapshot> [--store DIR] [--nodes N] [--replicas R]
snapvault restore  <snapshot> <dir> [--store DIR] [--parallel N]
snapvault fail-node <id> [--store DIR]
snapvault recover  [--store DIR]
snapvault status   [--store DIR]
```

`put` distributes a snapshot's chunks across the simulated nodes with
replication. `restore` fetches chunks concurrently from whichever nodes are up,
verifies each chunk's hash on arrival, and reassembles the files. `fail-node`
and `recover` drive the failure simulation; `status` reports per-node chunk
counts and replication health.

## `make demo` output

The demo synthesizes a mock dataset (including a duplicated file), takes a
snapshot, edits one file and takes an incremental snapshot, distributes the
chunks with replication, fails a node, parallel-restores, and confirms the
restored tree matches the original byte-for-byte:

```
==> 2. C++ engine: first snapshot (content-addressed, dedup)
snapshot 'v1' created
  files        : 4
  bytes        : 243826
  chunk refs   : 63
  new chunks   : 33
  deduped      : 30

==> 3. C++ engine: edit one file, take an incremental snapshot
snapshot 'v2' created
  files        : 4
  chunk refs   : 63
  new chunks   : 1
  deduped      : 62

==> 4. Go layer: distribute v2 across simulated nodes with replication
distributed snapshot 'v2'
  nodes        : 5
  replicas     : 3
  unique chunks: 33
  chunk copies : 99

==> 5. simulate a node failure, then parallel restore with verification
node 2 marked down
cluster: 5 nodes, replication 3
  node 0: up    chunks=19
  node 1: up    chunks=22
  node 2: DOWN  chunks=22
  node 3: up    chunks=20
  node 4: up    chunks=19
replication health: 4/5 nodes up
restored snapshot 'v2' to demo/restored
  files        : 4
  chunks       : 33
  verified     : 33
  parallelism  : 4

==> 6. integrity check: restored tree vs original (byte-for-byte)
    NODE FAILURE SURVIVED and INTEGRITY VERIFIED: restored tree matches original byte-for-byte
```

The first snapshot deduplicates the identical file (30 of 63 chunk references
are already present); the incremental snapshot writes a single new chunk for
the one edited file. After a node is taken down, all 33 chunks are still fetched
and hash-verified from surviving replicas, and the restored tree hashes
identically to the original.

## Layout

```
core/                C++17 engine (CMake): svcore + svcore_lib + CTest suites
cmd/snapvault/       Go CLI
internal/store/      shared-format store reader (Go)
internal/cluster/    simulated nodes, replication, deterministic placement
internal/distribute/ parallel verified restore + failure handling
scripts/demo.sh      the make demo pipeline
FORMAT.md            the shared on-disk format (chunks + JSON manifest)
ARCHITECTURE.md      design and failure model
```

## Requirements

- CMake 3.16+ and a C++17 compiler (clang or gcc)
- Go 1.22+ (standard library only)

## License

MIT, Sai Asish Y. See [LICENSE](LICENSE).
