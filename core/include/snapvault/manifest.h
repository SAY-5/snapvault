#ifndef SNAPVAULT_MANIFEST_H
#define SNAPVAULT_MANIFEST_H

#include <cstdint>
#include <string>
#include <vector>

namespace snapvault {

// One file within a snapshot: its path (relative, '/'-separated), total size,
// and the ordered list of chunk content-addresses that reconstruct it.
struct FileEntry {
    std::string path;
    uint64_t size = 0;
    std::vector<std::string> chunks;
};

// A snapshot manifest: a named, versioned list of files. Serialized to JSON.
struct Manifest {
    std::string name;
    uint32_t chunk_size = 0;
    // Monotonically increasing sequence number assigned by the engine at
    // snapshot time; orders snapshots oldest to newest. 0 in manifests
    // written before the field existed (treated as oldest).
    uint64_t seq = 0;
    std::vector<FileEntry> files;

    // Serialize to the on-disk JSON format documented in FORMAT.md.
    std::string to_json() const;

    // Parse JSON produced by to_json(). Throws std::runtime_error on malformed
    // input. This is a focused parser for our own schema, not general JSON.
    static Manifest from_json(const std::string& text);
};

}  // namespace snapvault

#endif  // SNAPVAULT_MANIFEST_H
