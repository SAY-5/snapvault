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

    // Snapshots get increasing sequence numbers regardless of name order.
    {
        std::string src = svtest::temp_dir("seq_src");
        std::string store = svtest::temp_dir("seq_store");
        write(fsutil::join(src, "f.bin"), distinct("seq", 1024));

        Engine eng(store, kChunk);
        eng.snapshot("zzz", src);
        eng.snapshot("aaa", src);
        CHECK_EQ(eng.load_manifest("zzz").seq, static_cast<uint64_t>(1));
        CHECK_EQ(eng.load_manifest("aaa").seq, static_cast<uint64_t>(2));
    }

    // retain --keep-last 2 keeps the two newest snapshots (by sequence, not
    // name), drops the older ones, and gc reclaims their exclusive chunks.
    {
        std::string src = svtest::temp_dir("retain_src");
        std::string store = svtest::temp_dir("retain_store");
        write(fsutil::join(src, "base.bin"), distinct("base", 2048));  // 2 chunks

        Engine eng(store, kChunk);
        // Four generations, each adding one exclusive 1-chunk file. Names are
        // deliberately not in chronological order.
        const char* names[] = {"delta", "alpha", "omega", "beta"};
        for (int g = 0; g < 4; ++g) {
            write(fsutil::join(src, std::string("gen") + std::to_string(g) + ".bin"),
                  distinct(std::string("gen") + std::to_string(g), 1024));
            // Each generation replaces the previous exclusive file.
            if (g > 0) {
                fsutil::remove_file(
                    fsutil::join(src, std::string("gen") + std::to_string(g - 1) + ".bin"));
            }
            eng.snapshot(names[g], src);
        }
        // Store now holds base (2) + gen0..gen3 (4) = 6 unique chunks.
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(6));

        RetainStats s = eng.retain(2);
        CHECK_EQ(s.kept.size(), static_cast<size_t>(2));
        CHECK_EQ(s.kept[0], std::string("beta"));    // newest
        CHECK_EQ(s.kept[1], std::string("omega"));
        CHECK_EQ(s.dropped.size(), static_cast<size_t>(2));
        CHECK_EQ(s.dropped[0], std::string("delta"));  // oldest
        CHECK_EQ(s.dropped[1], std::string("alpha"));

        // gen0 and gen1 chunks are gone; base, gen2, gen3 survive.
        CHECK_EQ(s.gc.removed, static_cast<uint64_t>(2));
        CHECK_EQ(chunk_count(eng), static_cast<size_t>(4));
        CHECK(eng.verify("beta").ok());
        CHECK(eng.verify("omega").ok());

        // The kept snapshots still restore.
        std::string out = svtest::temp_dir("retain_out");
        eng.restore("beta", out);
        CHECK(fsutil::exists(fsutil::join(out, "gen3.bin")));
    }

    // Keeping more snapshots than exist drops nothing.
    {
        std::string src = svtest::temp_dir("retain_all_src");
        std::string store = svtest::temp_dir("retain_all_store");
        write(fsutil::join(src, "a.bin"), distinct("all", 1024));

        Engine eng(store, kChunk);
        eng.snapshot("one", src);
        eng.snapshot("two", src);

        RetainStats s = eng.retain(5);
        CHECK_EQ(s.kept.size(), static_cast<size_t>(2));
        CHECK_EQ(s.dropped.size(), static_cast<size_t>(0));
        CHECK_EQ(s.gc.removed, static_cast<uint64_t>(0));
    }

    // keep_last of 0 is rejected.
    {
        std::string store = svtest::temp_dir("retain_zero_store");
        Engine eng(store, kChunk);
        bool threw = false;
        try {
            eng.retain(0);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }

    return svtest::summary("retain");
}
