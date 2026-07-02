#include "snapvault/chunker.h"

#include <algorithm>

#include "snapvault/sha256.h"

namespace snapvault {

std::vector<Chunk> chunk_buffer(const std::string& buffer, size_t chunk_size) {
    if (chunk_size == 0) chunk_size = kDefaultChunkSize;
    std::vector<Chunk> chunks;
    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t len = std::min(chunk_size, buffer.size() - offset);
        Chunk c;
        c.data = buffer.substr(offset, len);
        c.hash = Sha256::hex(c.data);
        chunks.push_back(std::move(c));
        offset += len;
    }
    return chunks;
}

}  // namespace snapvault
