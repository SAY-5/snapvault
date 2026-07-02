# snapvault on-disk format

Both the C++ storage engine (`svcore`) and the Go distribution layer
(`snapvault`) read and write the same content store. This document is the
single source of truth for that format; changing it means changing both sides.

## Store layout

```
<store-root>/
  chunks/
    <hash>            raw chunk bytes, one file per unique chunk
  snapshots/
    <name>.json       snapshot manifest (JSON)
```

## Chunks

- A chunk is a contiguous slice of a file's bytes.
- The C++ engine uses fixed-size chunking with a configurable chunk size
  (default 4096 bytes); the final chunk of a file may be smaller.
- Each chunk is content-addressed: its identity is the lowercase hex
  SHA-256 (FIPS 180-4) of its bytes, exactly 64 hex characters.
- The chunk is stored at `chunks/<hash>` with its raw, unmodified bytes.
- Storing a chunk whose hash already exists is a no-op (deduplication).
- Integrity check: re-hashing the bytes of `chunks/<hash>` must reproduce
  `<hash>`. Any mismatch is corruption.
- Lifecycle: a chunk is live while at least one manifest references it.
  Deleting a manifest (`svcore drop`) un-references its chunks; a garbage
  collection pass (`svcore gc`) may then delete any chunk file referenced by
  no manifest. gc never touches referenced chunks.

## Manifest

A snapshot manifest is a JSON object at `snapshots/<name>.json`:

```json
{
  "name": "daily-2026",
  "chunk_size": 4096,
  "files": [
    {
      "path": "dir/one.dat",
      "size": 5142,
      "chunks": ["<hash0>", "<hash1>", "<hash2>"]
    }
  ]
}
```

Fields:

- `name` (string): the snapshot's identifier, matching the file name stem.
- `chunk_size` (number): the chunk size used when the snapshot was taken.
- `files` (array): one entry per regular file, ordered by path.
  - `path` (string): file path relative to the snapshot root, using `/`
    separators.
  - `size` (number): total file size in bytes.
  - `chunks` (array of strings): the ordered chunk hashes. Concatenating the
    bytes of these chunks, in order, reproduces the file byte-for-byte.

The JSON is standard and parses with Go's `encoding/json` and the C++
engine's schema parser alike.

## Distribution (Go layer only)

The Go layer adds a placement plan on top of the store. It never changes the
chunk or manifest format; it decides which simulated nodes hold a replica of
each chunk. Placement is deterministic given the chunk hash, node count, and
replication factor, so no wall-clock or random state affects correctness.
