#include "snapvault/engine.h"

#include <set>
#include <stdexcept>

#include "snapvault/chunker.h"
#include "snapvault/fs_util.h"
#include "snapvault/sha256.h"

namespace snapvault {

Engine::Engine(std::string root, size_t chunk_size)
    : store_(root),
      root_(std::move(root)),
      snapshots_dir_(fsutil::join(root_, "snapshots")),
      chunk_size_(chunk_size == 0 ? kDefaultChunkSize : chunk_size) {}

std::string Engine::manifest_path(const std::string& name) const {
    return fsutil::join(snapshots_dir_, name + ".json");
}

SnapshotStats Engine::snapshot(const std::string& name, const std::string& dir) {
    if (!fsutil::is_dir(dir)) {
        throw std::runtime_error("snapshot source is not a directory: " + dir);
    }
    store_.init();
    fsutil::make_dirs(snapshots_dir_);

    Manifest manifest;
    manifest.name = name;
    manifest.chunk_size = static_cast<uint32_t>(chunk_size_);

    SnapshotStats stats;
    for (const std::string& rel : fsutil::list_files(dir)) {
        std::string content = fsutil::read_file(fsutil::join(dir, rel));
        FileEntry fe;
        fe.path = rel;
        fe.size = content.size();
        stats.bytes += content.size();

        for (Chunk& chunk : chunk_buffer(content, chunk_size_)) {
            fe.chunks.push_back(chunk.hash);
            ++stats.chunks;
            // Incremental: only newly hashed content is written; unchanged
            // chunks are referenced, not re-stored.
            if (store_.put(chunk.hash, chunk.data)) {
                ++stats.new_chunks;
            } else {
                ++stats.deduped_chunks;
            }
        }
        manifest.files.push_back(std::move(fe));
        ++stats.files;
    }

    fsutil::write_file(manifest_path(name), manifest.to_json());
    return stats;
}

Manifest Engine::load_manifest(const std::string& name) const {
    std::string path = manifest_path(name);
    if (!fsutil::exists(path)) {
        throw std::runtime_error("snapshot not found: " + name);
    }
    return Manifest::from_json(fsutil::read_file(path));
}

void Engine::restore(const std::string& name, const std::string& out_dir) const {
    Manifest manifest = load_manifest(name);
    fsutil::make_dirs(out_dir);
    for (const FileEntry& fe : manifest.files) {
        std::string content;
        content.reserve(fe.size);
        for (const std::string& hash : fe.chunks) {
            content += store_.get(hash);
        }
        fsutil::write_file(fsutil::join(out_dir, fe.path), content);
    }
}

VerifyResult Engine::verify(const std::string& name) const {
    VerifyResult result;
    std::set<std::string> hashes;

    if (name.empty()) {
        // Verify all chunks referenced by every snapshot.
        std::string sdir = snapshots_dir_;
        if (fsutil::is_dir(sdir)) {
            for (const std::string& rel : fsutil::list_files(sdir)) {
                Manifest m = Manifest::from_json(
                    fsutil::read_file(fsutil::join(sdir, rel)));
                for (const FileEntry& fe : m.files) {
                    for (const std::string& h : fe.chunks) hashes.insert(h);
                }
            }
        }
    } else {
        Manifest m = load_manifest(name);
        for (const FileEntry& fe : m.files) {
            for (const std::string& h : fe.chunks) hashes.insert(h);
        }
    }

    for (const std::string& h : hashes) {
        std::string data = store_.get(h);
        std::string actual = Sha256::hex(data);
        ++result.checked;
        if (actual != h) {
            ++result.bad;
            if (result.first_bad_hash.empty()) result.first_bad_hash = h;
        }
    }
    return result;
}

}  // namespace snapvault
