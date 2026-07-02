#ifndef SNAPVAULT_ENGINE_H
#define SNAPVAULT_ENGINE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "snapvault/chunker.h"
#include "snapvault/content_store.h"
#include "snapvault/manifest.h"

namespace snapvault {

// Statistics from a snapshot operation.
struct SnapshotStats {
    uint64_t files = 0;
    uint64_t chunks = 0;       // total chunk references across all files
    uint64_t new_chunks = 0;   // chunks newly written to the store
    uint64_t deduped_chunks = 0;  // chunks skipped because already present
    uint64_t bytes = 0;        // total bytes across all files
};

// Result of a garbage collection pass.
struct GcStats {
    uint64_t scanned = 0;      // chunk files inspected in the store
    uint64_t referenced = 0;   // chunks referenced by at least one manifest
    uint64_t removed = 0;      // candidates deleted (0 on a dry run)
    uint64_t bytes_reclaimed = 0;  // bytes of the candidate chunks
    std::vector<std::string> candidates;  // hashes eligible for removal
};

// Comparison of two snapshots, oldest first.
struct DiffResult {
    std::vector<std::string> added;    // paths only in the newer snapshot
    std::vector<std::string> removed;  // paths only in the older snapshot
    std::vector<std::string> changed;  // paths in both whose chunks differ
    uint64_t unchanged = 0;            // paths in both with identical chunks
    uint64_t chunks_added = 0;    // unique chunks only in the newer snapshot
    uint64_t chunks_removed = 0;  // unique chunks only in the older snapshot
    uint64_t chunks_shared = 0;   // unique chunks common to both
};

// Result of a retention pass: which snapshots were kept and dropped, and the
// gc that ran afterwards.
struct RetainStats {
    std::vector<std::string> kept;     // newest first
    std::vector<std::string> dropped;  // oldest first
    GcStats gc;
};

// Result of a verify pass.
struct VerifyResult {
    uint64_t checked = 0;
    uint64_t bad = 0;
    std::string first_bad_hash;  // empty if none
    bool ok() const { return bad == 0; }
};

// The storage engine ties a content store to snapshot manifests.
//
// Manifests live at <root>/snapshots/<name>.json (see FORMAT.md).
class Engine {
public:
    explicit Engine(std::string root, size_t chunk_size = kDefaultChunkSize);

    // Walk `dir`, chunk every file, store new chunks (dedup existing), and
    // write a manifest named `name`. Incremental: only chunks whose hash is
    // not already in the store are written.
    SnapshotStats snapshot(const std::string& name, const std::string& dir);

    // Load a manifest by name.
    Manifest load_manifest(const std::string& name) const;

    // Rebuild every file of snapshot `name` under `out_dir`, byte-for-byte.
    void restore(const std::string& name, const std::string& out_dir) const;

    // Re-hash every chunk referenced by snapshot `name` (or all stored chunks
    // if name is empty) and confirm each matches its content-address.
    VerifyResult verify(const std::string& name = "") const;

    // Delete the manifest for snapshot `name`. The snapshot's chunks stay in
    // the store until a gc pass finds them unreferenced.
    void drop(const std::string& name);

    // Remove chunks referenced by no manifest. With dry_run, only report the
    // candidates without deleting anything.
    GcStats gc(bool dry_run = false);

    // Compare two snapshots: files added, removed, and changed going from
    // `a` to `b`, plus chunk-level stats.
    DiffResult diff(const std::string& a, const std::string& b) const;

    // Keep the newest `keep_last` snapshots (by sequence number), drop the
    // rest, then gc the store. keep_last must be >= 1.
    RetainStats retain(size_t keep_last);

    // Names of all snapshots in the store, sorted.
    std::vector<std::string> snapshot_names() const;

    const ContentStore& store() const { return store_; }
    std::string manifest_path(const std::string& name) const;

private:
    ContentStore store_;
    std::string root_;
    std::string snapshots_dir_;
    size_t chunk_size_;
};

}  // namespace snapvault

#endif  // SNAPVAULT_ENGINE_H
