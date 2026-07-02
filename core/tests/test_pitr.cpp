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

    // Point-in-time restore: after taking v2 with edits, deletions, and new
    // files, restoring at v1 reproduces the original dataset byte-for-byte.
    {
        std::string src = svtest::temp_dir("pitr_src");
        std::string store = svtest::temp_dir("pitr_store");
        std::string v1_edit = distinct("report", 5000);
        std::string v1_gone = distinct("scratch", 2000);
        std::string keep = distinct("keep", 3000);
        write(fsutil::join(src, "docs/report.dat"), v1_edit);
        write(fsutil::join(src, "scratch.tmp"), v1_gone);
        write(fsutil::join(src, "keep.bin"), keep);

        Engine eng(store, kChunk);
        eng.snapshot("v1", src);

        // Mutate the dataset in every way: edit, delete, add.
        write(fsutil::join(src, "docs/report.dat"), distinct("rewritten", 6000));
        fsutil::remove_file(fsutil::join(src, "scratch.tmp"));
        write(fsutil::join(src, "brand-new.bin"), distinct("new", 1500));
        eng.snapshot("v2", src);

        // Restoring at v1 brings back the historical dataset exactly.
        std::string at_v1 = svtest::temp_dir("pitr_out_v1");
        eng.restore("v1", at_v1);
        CHECK_EQ(fsutil::read_file(fsutil::join(at_v1, "docs/report.dat")), v1_edit);
        CHECK_EQ(fsutil::read_file(fsutil::join(at_v1, "scratch.tmp")), v1_gone);
        CHECK_EQ(fsutil::read_file(fsutil::join(at_v1, "keep.bin")), keep);
        CHECK(!fsutil::exists(fsutil::join(at_v1, "brand-new.bin")));

        // And the latest snapshot still restores its own view.
        std::string at_v2 = svtest::temp_dir("pitr_out_v2");
        eng.restore("v2", at_v2);
        CHECK(!fsutil::exists(fsutil::join(at_v2, "scratch.tmp")));
        CHECK(fsutil::exists(fsutil::join(at_v2, "brand-new.bin")));
        CHECK_EQ(fsutil::read_file(fsutil::join(at_v2, "keep.bin")), keep);
    }

    // Historical restores stay valid after a gc: v1's chunks are referenced,
    // so reclaiming a dropped third snapshot cannot touch them.
    {
        std::string src = svtest::temp_dir("pitr_gc_src");
        std::string store = svtest::temp_dir("pitr_gc_store");
        std::string original = distinct("original", 4000);
        write(fsutil::join(src, "data.bin"), original);

        Engine eng(store, kChunk);
        eng.snapshot("v1", src);
        write(fsutil::join(src, "data.bin"), distinct("second", 4000));
        eng.snapshot("v2", src);
        write(fsutil::join(src, "data.bin"), distinct("third", 4000));
        eng.snapshot("temp", src);

        eng.drop("temp");
        GcStats gc = eng.gc();
        CHECK(gc.removed > 0);

        std::string out = svtest::temp_dir("pitr_gc_out");
        eng.restore("v1", out);
        CHECK_EQ(fsutil::read_file(fsutil::join(out, "data.bin")), original);
        CHECK(eng.verify("v1").ok());
    }

    return svtest::summary("pitr");
}
