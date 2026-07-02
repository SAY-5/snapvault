#include <fstream>
#include <string>

#include "snapvault/engine.h"
#include "snapvault/fs_util.h"
#include "snapvault/manifest.h"
#include "test_util.h"

using namespace snapvault;

namespace {

void write(const std::string& path, const std::string& data) {
    fsutil::write_file(path, data);
}

std::string big(char c, size_t n) { return std::string(n, c); }

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

    // Dedup: two byte-for-byte identical files store their chunks once. Each
    // file is exactly 4 distinct 1024-byte chunks (last is 928 bytes).
    {
        std::string src = svtest::temp_dir("dedup_src");
        std::string store = svtest::temp_dir("dedup_store");
        std::string content = distinct("same", 4000);
        write(fsutil::join(src, "a.bin"), content);
        write(fsutil::join(src, "b.bin"), content);  // identical content

        Engine eng(store, kChunk);
        SnapshotStats s = eng.snapshot("s1", src);
        CHECK_EQ(s.files, static_cast<uint64_t>(2));
        // 4000 bytes / 1024 = 4 chunks per file, 8 references total.
        CHECK_EQ(s.chunks, static_cast<uint64_t>(8));
        // Identical files: only one file's worth of unique chunks written.
        CHECK_EQ(s.new_chunks, static_cast<uint64_t>(4));
        CHECK_EQ(s.deduped_chunks, static_cast<uint64_t>(4));
    }

    // Incremental: a second snapshot after changing one file adds only the
    // changed chunks; the untouched file's chunks are all deduped.
    {
        std::string src = svtest::temp_dir("incr_src");
        std::string store = svtest::temp_dir("incr_store");
        write(fsutil::join(src, "keep.bin"), distinct("keep", 3000));  // 3 chunks
        write(fsutil::join(src, "edit.bin"), distinct("edit", 3000));  // 3 chunks

        Engine eng(store, kChunk);
        SnapshotStats first = eng.snapshot("v1", src);
        CHECK_EQ(first.new_chunks, static_cast<uint64_t>(6));
        CHECK_EQ(first.deduped_chunks, static_cast<uint64_t>(0));

        // Change only edit.bin; keep.bin is untouched.
        write(fsutil::join(src, "edit.bin"), distinct("changed", 3000));
        SnapshotStats second = eng.snapshot("v2", src);

        // keep.bin (3 chunks) is deduped; edit.bin (3 chunks) is new.
        CHECK_EQ(second.new_chunks, static_cast<uint64_t>(3));
        CHECK_EQ(second.deduped_chunks, static_cast<uint64_t>(3));
    }

    // Restore round-trips byte-for-byte, including nested directories.
    {
        std::string src = svtest::temp_dir("rt_src");
        std::string store = svtest::temp_dir("rt_store");
        std::string out = svtest::temp_dir("rt_out");
        std::string f1 = big('a', 100) + big('b', 5000) + big('c', 42);
        std::string f2 = "small file";
        write(fsutil::join(src, "dir/one.dat"), f1);
        write(fsutil::join(src, "two.txt"), f2);

        Engine eng(store, kChunk);
        eng.snapshot("rt", src);
        eng.restore("rt", out);

        CHECK_EQ(fsutil::read_file(fsutil::join(out, "dir/one.dat")), f1);
        CHECK_EQ(fsutil::read_file(fsutil::join(out, "two.txt")), f2);
    }

    // Verify passes on a clean store and detects a corrupted chunk.
    {
        std::string src = svtest::temp_dir("vf_src");
        std::string store = svtest::temp_dir("vf_store");
        write(fsutil::join(src, "data.bin"), big('D', 5000));

        Engine eng(store, kChunk);
        Manifest m;
        eng.snapshot("vf", src);

        VerifyResult clean = eng.verify("vf");
        CHECK(clean.ok());
        CHECK(clean.checked > 0);

        // Corrupt one stored chunk by overwriting its bytes.
        m = eng.load_manifest("vf");
        std::string victim = m.files[0].chunks[0];
        std::string path = eng.store().chunk_path(victim);
        write(path, "corrupted contents that will not rehash to the address");

        VerifyResult dirty = eng.verify("vf");
        CHECK(!dirty.ok());
        CHECK_EQ(dirty.bad, static_cast<uint64_t>(1));
        CHECK_EQ(dirty.first_bad_hash, victim);
    }

    // Manifest JSON survives a serialize/parse round-trip.
    {
        Manifest m;
        m.name = "round";
        m.chunk_size = 4096;
        FileEntry fe;
        fe.path = "a/b.txt";
        fe.size = 10;
        fe.chunks = {"aa", "bb", "cc"};
        m.files.push_back(fe);

        Manifest back = Manifest::from_json(m.to_json());
        CHECK_EQ(back.name, std::string("round"));
        CHECK_EQ(back.chunk_size, static_cast<uint32_t>(4096));
        CHECK_EQ(back.files.size(), static_cast<size_t>(1));
        CHECK_EQ(back.files[0].path, std::string("a/b.txt"));
        CHECK_EQ(back.files[0].chunks.size(), static_cast<size_t>(3));
        CHECK_EQ(back.files[0].chunks[2], std::string("cc"));
    }

    return svtest::summary("snapshot");
}
