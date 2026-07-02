#ifndef SNAPVAULT_CHUNKER_H
#define SNAPVAULT_CHUNKER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace snapvault {

// Default chunk size (bytes) for fixed-size chunking.
constexpr size_t kDefaultChunkSize = 4096;

// A single chunk: its content-address (hex SHA-256) and its raw bytes.
struct Chunk {
    std::string hash;
    std::string data;
};

// Split a buffer into fixed-size chunks, each hashed by content.
// The final chunk may be smaller than chunk_size.
std::vector<Chunk> chunk_buffer(const std::string& buffer,
                                size_t chunk_size = kDefaultChunkSize);

}  // namespace snapvault

#endif  // SNAPVAULT_CHUNKER_H
