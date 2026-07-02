#ifndef SNAPVAULT_ENGINE_H
#define SNAPVAULT_ENGINE_H

#include <cstddef>
#include <cstdint>
#include <string>

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
