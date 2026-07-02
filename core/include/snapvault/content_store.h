#ifndef SNAPVAULT_CONTENT_STORE_H
#define SNAPVAULT_CONTENT_STORE_H

#include <string>

namespace snapvault {

// Content-addressed storage on the local filesystem.
//
// Layout (see FORMAT.md):
//   <root>/chunks/<hash>   raw chunk bytes, file named by its hex SHA-256
//
// Writing a chunk whose hash already exists is a no-op (dedup).
class ContentStore {
public:
    explicit ContentStore(std::string root);

    // Ensure the store directory layout exists.
    void init() const;

    // Store a chunk. Returns true if it was newly written, false if the
    // content-address already existed (deduplicated).
    bool put(const std::string& hash, const std::string& data) const;

    // True if a chunk with this hash is present.
    bool has(const std::string& hash) const;

    // Read a chunk's bytes. Throws std::runtime_error if missing.
    std::string get(const std::string& hash) const;

    // Absolute path where a chunk with this hash lives.
    std::string chunk_path(const std::string& hash) const;

    const std::string& root() const { return root_; }

private:
    std::string root_;
    std::string chunks_dir_;
};

}  // namespace snapvault

#endif  // SNAPVAULT_CONTENT_STORE_H
