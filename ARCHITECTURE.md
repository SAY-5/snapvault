# snapvault architecture

snapvault is a distributed backup and rapid-restore service split across two
languages that share one on-disk format. The C++ engine owns storage; the Go
layer owns distribution. Neither talks to a network or a real cluster, so the
whole system is reproducible on a single machine.

```
              +---------------------------+
  files  -->  |  C++ engine (svcore)      |  content-addressed storage
              |  - fixed-size chunker     |
              |  - SHA-256 (from scratch) |
              |  - dedup content store    |
              |  - snapshot manifests     |
              +-------------+-------------+
                            | writes chunks/ + snapshots/ (FORMAT.md)
                            v
              +---------------------------+
  restore <-- |  Go layer (snapvault)     |  distributed layer
              |  - N simulated nodes      |
              |  - replication factor R   |
              |  - deterministic placement|
              |  - parallel verified fetch|
              |  - node failure/recovery  |
              +---------------------------+
```

## C++ engine (`core/`)

The storage half. Files are split into fixed-size chunks; each chunk is
identified by the lowercase hex SHA-256 of its bytes and stored once at
`chunks/<hash>`. Writing a chunk whose hash already exists is a no-op, which
gives deduplication for free: identical files, and identical regions across
files, share stored chunks.

A snapshot is a manifest (`snapshots/<name>.json`) listing every file and the
ordered chunk hashes that reconstruct it. Snapshots are incremental by
construction: a later snapshot only writes chunks whose hash is new; unchanged
content is referenced, not re-stored.

- `sha256.{h,cpp}` a compact, dependency-free FIPS 180-4 implementation.
- `chunker.{h,cpp}` splits a buffer into content-hashed chunks.
- `content_store.{h,cpp}` the dedup-aware chunk store on the filesystem.
- `manifest.{h,cpp}` serializes and parses the shared JSON manifest schema.
- `engine.{h,cpp}` ties it together: `snapshot`, `restore`, `verify`.
- `main.cpp` the `svcore` CLI.

`verify` re-hashes stored chunks against their content-address and validates a
snapshot's manifest, so corruption is caught before a restore trusts the data.

## Go layer (`cmd/snapvault`, `internal/*`)

The distribution half sits on top of the exact same store.

- `internal/store` reads the C++-written chunks and manifests (shared format).
- `internal/cluster` models N nodes with replication factor R. Placement is a
  pure function of the chunk hash, node count, and R: the replica set is R
  consecutive node ids starting from `fnv(hash) mod N`. No wall-clock or random
  state affects correctness, so a run is fully reproducible. Nodes can be marked
  down and recovered; the down-set persists in `cluster.json`.
- `internal/distribute` uploads a snapshot's unique chunks to their replica
  nodes, and restores in parallel: a pool of worker goroutines fetches chunks
  concurrently from whichever replica is up, verifies each chunk's
  content-address on arrival, and a shared context cancels the whole restore on
  the first integrity failure or unavailable chunk.

## Failure model

With replication factor R, a chunk survives up to R-1 node failures among its
replica set. A restore reads each chunk from the first up replica that holds
it. If every replica of some chunk is down, the restore fails cleanly with
`ErrChunkUnavailable` rather than producing a corrupt file. Recovery marks
nodes up again and a subsequent restore succeeds.

`repair` closes the window between failures: it scans every chunk, and any
chunk whose live replica count (copies on up nodes) fell below R is copied
from a surviving replica to healthy nodes chosen deterministically, so the
cluster tolerates the next R-1 failures again. Repaired replicas can live
outside the derived placement set; reads fall back to them and their
locations persist in `cluster.json`. `add-node` and `remove-node` change the
topology: placement is a function of the node count, so both trigger a
rebalance that moves each chunk onto its new placement set (a deterministic,
reported set of copies).

## Why the two-language split

The engine is CPU-bound, byte-exact work (hashing, chunking, filesystem I/O)
that maps naturally to modern C++17. The distribution layer is concurrency and
coordination work (parallel fetch, failure handling) that maps naturally to Go
goroutines. Keeping one shared on-disk format (see `FORMAT.md`) lets each half
be built, tested, and reasoned about independently while interoperating
exactly.
