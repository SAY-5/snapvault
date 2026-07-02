#include "snapvault/engine.h"

#include <algorithm>
#include <map>
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

    // Order snapshots by a monotonically increasing sequence number.
    uint64_t max_seq = 0;
    for (const std::string& existing : snapshot_names()) {
        Manifest prev = load_manifest(existing);
        if (prev.seq > max_seq) max_seq = prev.seq;
    }
    manifest.seq = max_seq + 1;

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

std::vector<std::string> Engine::snapshot_names() const {
    std::vector<std::string> names;
    if (!fsutil::is_dir(snapshots_dir_)) return names;
    for (const std::string& rel : fsutil::list_files(snapshots_dir_)) {
        const std::string ext = ".json";
        if (rel.size() > ext.size() &&
            rel.compare(rel.size() - ext.size(), ext.size(), ext) == 0) {
            names.push_back(rel.substr(0, rel.size() - ext.size()));
        }
    }
    return names;
}

void Engine::drop(const std::string& name) {
    std::string path = manifest_path(name);
    if (!fsutil::exists(path)) {
        throw std::runtime_error("snapshot not found: " + name);
    }
    fsutil::remove_file(path);
}

GcStats Engine::gc(bool dry_run) {
    // Everything referenced by any remaining manifest is live.
    std::set<std::string> live;
    for (const std::string& name : snapshot_names()) {
        Manifest m = load_manifest(name);
        for (const FileEntry& fe : m.files) {
            for (const std::string& h : fe.chunks) live.insert(h);
        }
    }

    GcStats stats;
    stats.referenced = live.size();
    std::string chunks_dir = fsutil::join(root_, "chunks");
    if (!fsutil::is_dir(chunks_dir)) return stats;
    for (const std::string& hash : fsutil::list_files(chunks_dir)) {
        ++stats.scanned;
        if (live.count(hash)) continue;
        stats.bytes_reclaimed += fsutil::file_size(store_.chunk_path(hash));
        stats.candidates.push_back(hash);
        if (!dry_run) {
            fsutil::remove_file(store_.chunk_path(hash));
            ++stats.removed;
        }
    }
    return stats;
}

DiffResult Engine::diff(const std::string& a, const std::string& b) const {
    Manifest ma = load_manifest(a);
    Manifest mb = load_manifest(b);

    std::map<std::string, const FileEntry*> in_a, in_b;
    for (const FileEntry& fe : ma.files) in_a[fe.path] = &fe;
    for (const FileEntry& fe : mb.files) in_b[fe.path] = &fe;

    DiffResult d;
    for (const auto& [path, fe] : in_b) {
        auto it = in_a.find(path);
        if (it == in_a.end()) {
            d.added.push_back(path);
        } else if (it->second->chunks != fe->chunks) {
            d.changed.push_back(path);
        } else {
            ++d.unchanged;
        }
    }
    for (const auto& [path, fe] : in_a) {
        (void)fe;
        if (!in_b.count(path)) d.removed.push_back(path);
    }

    std::set<std::string> ca, cb;
    for (const FileEntry& fe : ma.files) ca.insert(fe.chunks.begin(), fe.chunks.end());
    for (const FileEntry& fe : mb.files) cb.insert(fe.chunks.begin(), fe.chunks.end());
    for (const std::string& h : cb) {
        if (ca.count(h)) ++d.chunks_shared;
        else ++d.chunks_added;
    }
    for (const std::string& h : ca) {
        if (!cb.count(h)) ++d.chunks_removed;
    }
    return d;
}

RetainStats Engine::retain(size_t keep_last) {
    if (keep_last < 1) {
        throw std::runtime_error("retain requires keeping at least 1 snapshot");
    }

    // Sort newest first: by sequence number, name as a tie-break.
    std::vector<Manifest> all;
    for (const std::string& name : snapshot_names()) {
        all.push_back(load_manifest(name));
    }
    std::sort(all.begin(), all.end(), [](const Manifest& x, const Manifest& y) {
        if (x.seq != y.seq) return x.seq > y.seq;
        return x.name > y.name;
    });

    RetainStats stats;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i < keep_last) {
            stats.kept.push_back(all[i].name);
        } else {
            stats.dropped.push_back(all[i].name);
        }
    }
    // Drop oldest first so the list reads chronologically.
    std::reverse(stats.dropped.begin(), stats.dropped.end());
    for (const std::string& name : stats.dropped) {
        drop(name);
    }
    stats.gc = gc();
    return stats;
}

}  // namespace snapvault
