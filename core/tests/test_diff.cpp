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

}  // namespace

int main() {
    const size_t kChunk = 1024;

    // The demo scenario: edit one file between snapshots. The diff reports
    // exactly that file as changed and everything else as unchanged.
    {
        std::string src = svtest::temp_dir("diff_src");
        std::string store = svtest::temp_dir("diff_store");
        write(fsutil::join(src, "keep.bin"), distinct("keep", 3072));   // 3 chunks
        write(fsutil::join(src, "other.txt"), distinct("other", 2048)); // 2 chunks
        write(fsutil::join(src, "edit.bin"), distinct("edit", 3072));   // 3 chunks

        Engine eng(store, kChunk);
        eng.snapshot("v1", src);
        // Edit only the last chunk of edit.bin.
        std::string edited = distinct("edit", 3072);
        edited.replace(edited.size() - 20, 20, "XXXXXXXXXXXXXXXXXXXX");
        write(fsutil::join(src, "edit.bin"), edited);
        eng.snapshot("v2", src);

        DiffResult d = eng.diff("v1", "v2");
        CHECK_EQ(d.changed.size(), static_cast<size_t>(1));
        CHECK_EQ(d.changed[0], std::string("edit.bin"));
        CHECK_EQ(d.added.size(), static_cast<size_t>(0));
        CHECK_EQ(d.removed.size(), static_cast<size_t>(0));
        CHECK_EQ(d.unchanged, static_cast<uint64_t>(2));
        // Only edit.bin's last chunk was rewritten.
        CHECK_EQ(d.chunks_added, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_removed, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_shared, static_cast<uint64_t>(7));
    }

    // Added and removed files show up on the right sides of the diff.
    {
        std::string src = svtest::temp_dir("diff_ar_src");
        std::string store = svtest::temp_dir("diff_ar_store");
        write(fsutil::join(src, "stay.bin"), distinct("stay", 2048));  // 2 chunks
        write(fsutil::join(src, "gone.bin"), distinct("gone", 1024));  // 1 chunk

        Engine eng(store, kChunk);
        eng.snapshot("a", src);
        fsutil::remove_file(fsutil::join(src, "gone.bin"));
        write(fsutil::join(src, "fresh.bin"), distinct("fresh", 1024));  // 1 chunk
        eng.snapshot("b", src);

        DiffResult d = eng.diff("a", "b");
        CHECK_EQ(d.added.size(), static_cast<size_t>(1));
        CHECK_EQ(d.added[0], std::string("fresh.bin"));
        CHECK_EQ(d.removed.size(), static_cast<size_t>(1));
        CHECK_EQ(d.removed[0], std::string("gone.bin"));
        CHECK_EQ(d.changed.size(), static_cast<size_t>(0));
        CHECK_EQ(d.unchanged, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_added, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_removed, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_shared, static_cast<uint64_t>(2));
    }

    // Diffing a snapshot against itself is empty.
    {
        std::string src = svtest::temp_dir("diff_self_src");
        std::string store = svtest::temp_dir("diff_self_store");
        write(fsutil::join(src, "x.bin"), distinct("x", 2048));

        Engine eng(store, kChunk);
        eng.snapshot("only", src);
        DiffResult d = eng.diff("only", "only");
        CHECK_EQ(d.added.size(), static_cast<size_t>(0));
        CHECK_EQ(d.removed.size(), static_cast<size_t>(0));
        CHECK_EQ(d.changed.size(), static_cast<size_t>(0));
        CHECK_EQ(d.unchanged, static_cast<uint64_t>(1));
        CHECK_EQ(d.chunks_shared, static_cast<uint64_t>(2));
    }

    return svtest::summary("diff");
}
