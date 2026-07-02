#include <string>

#include "snapvault/engine.h"
#include "snapvault/fs_util.h"
#include "test_util.h"

using namespace snapvault;

namespace {

void write(const std::string& path, const std::string& data) {
    fsutil::write_file(path, data);
}

// Build `n` bytes whose every 1024-byte window is distinct, so fixed-size
// chunking yields all-unique chunks (no accidental intra-file dedup).
std::string distinct(const std::string& seed, size_t n) {
    std::string out;
    out.reserve(n);
    size_t i = 0;
    while (out.size() < n) {
        out += seed;
        out += std::to_string(i++);
        out += ':';
    }
    out.resize(n);
    return out;
}

size_t chunk_count(const Engine& eng) {
    std::string dir = fsutil::join(eng.store().root(), "chunks");
    return fsutil::list_files(dir).size();
}

}  // namespace

int main() {
    const size_t kChunk = 1024;

    // gc on a store with no dropped snapshots removes nothing.
    {
        std::string src = svtest::temp_dir("gc_noop_src");
        std::string store = svtest::temp_dir("gc_noop_store");
        write(fsutil::join(src, "a.bin"), distinct("noop", 3000));

        Engine eng(store, kChunk);
        eng.snapshot("s1", src);
        size_t before = chunk_count(eng);

        GcStats s = eng.gc();
        CHECK_EQ(s.scanned, static_cast<uint64_t>(before));
        CHECK_EQ(s.referenced, static_cast<uint64_t>(before));
        CHECK_EQ(s.removed, static_cast<uint64_t>(0));
        CHECK_EQ(s.candidates.size(), static_cast<size_t>(0));
        CHECK_EQ(chunk_count(eng), before);
    }

    // Drop then gc reclaims exactly the dropped snapshot's exclusive chunks,
    // but a chunk shared with a surviving snapshot stays and the survivor
    // still restores byte-for-byte.
    {
        std::string src = svtest::temp_dir("gc_shared_src");
        std::string store = svtest::temp_dir("gc_shared_store");
        std::string out = svtest::temp_dir("gc_shared_out");
        std::string shared = distinct("shared", 2048);  // 2 chunks in both snaps
        write(fsutil::join(src, "shared.bin"), shared);
        write(fsutil::join(src, "old.bin"), distinct("old", 3072));  // 3 chunks

        Engine eng(store, kChunk);
        eng.snapshot("v1", src);
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(5));

        // v2 keeps shared.bin but replaces old.bin with new content.
        fsutil::remove_file(fsutil::join(src, "old.bin"));
        write(fsutil::join(src, "new.bin"), distinct("new", 3072));  // 3 chunks
        eng.snapshot("v2", src);
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(8));

        eng.drop("v1");
        GcStats s = eng.gc();
        // Only old.bin's 3 chunks are unreferenced; the 2 shared.bin chunks
        // are still held by v2.
        CHECK_EQ(s.scanned, static_cast<uint64_t>(8));
        CHECK_EQ(s.referenced, static_cast<uint64_t>(5));
        CHECK_EQ(s.removed, static_cast<uint64_t>(3));
        CHECK_EQ(s.bytes_reclaimed, static_cast<uint64_t>(3072));
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(5));

        // v2 is intact after the collection.
        CHECK(eng.verify("v2").ok());
        eng.restore("v2", out);
        CHECK_EQ(fsutil::read_file(fsutil::join(out, "shared.bin")), shared);
    }

    // Dry run lists the candidates without deleting anything.
    {
        std::string src = svtest::temp_dir("gc_dry_src");
        std::string store = svtest::temp_dir("gc_dry_store");
        write(fsutil::join(src, "only.bin"), distinct("dry", 2048));  // 2 chunks

        Engine eng(store, kChunk);
        eng.snapshot("gone", src);
        eng.drop("gone");

        GcStats dry = eng.gc(/*dry_run=*/true);
        CHECK_EQ(dry.candidates.size(), static_cast<size_t>(2));
        CHECK_EQ(dry.removed, static_cast<uint64_t>(0));
        CHECK_EQ(dry.bytes_reclaimed, static_cast<uint64_t>(2048));
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(2));  // still on disk

        // A real gc then removes what the dry run reported.
        GcStats real = eng.gc();
        CHECK_EQ(real.removed, static_cast<uint64_t>(2));
        CHECK_EQ(real.candidates, dry.candidates);
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(0));
    }

    // Dropping an unknown snapshot fails loudly.
    {
        std::string store = svtest::temp_dir("gc_missing_store");
        Engine eng(store, kChunk);
        bool threw = false;
        try {
            eng.drop("never-existed");
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }

    return svtest::summary("gc");
}
