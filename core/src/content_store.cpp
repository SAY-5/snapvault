#include "snapvault/content_store.h"

#include <stdexcept>

#include "snapvault/fs_util.h"

namespace snapvault {

ContentStore::ContentStore(std::string root)
    : root_(std::move(root)), chunks_dir_(fsutil::join(root_, "chunks")) {}

void ContentStore::init() const {
    fsutil::make_dirs(chunks_dir_);
}

std::string ContentStore::chunk_path(const std::string& hash) const {
    return fsutil::join(chunks_dir_, hash);
}

bool ContentStore::has(const std::string& hash) const {
    return fsutil::exists(chunk_path(hash));
}

bool ContentStore::put(const std::string& hash, const std::string& data) const {
    if (has(hash)) return false;  // dedup: identical content already stored
    fsutil::write_file(chunk_path(hash), data);
    return true;
}

std::string ContentStore::get(const std::string& hash) const {
    if (!has(hash)) {
        throw std::runtime_error("chunk not found in store: " + hash);
    }
    return fsutil::read_file(chunk_path(hash));
}

}  // namespace snapvault
