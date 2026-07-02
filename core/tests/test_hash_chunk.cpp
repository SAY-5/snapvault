#include "snapvault/chunker.h"
#include "snapvault/content_store.h"
#include "snapvault/sha256.h"
#include "test_util.h"

using namespace snapvault;

int main() {
    // Known SHA-256 test vectors (FIPS 180-4 / NIST).
    CHECK_EQ(Sha256::hex(std::string("")),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(Sha256::hex(std::string("abc")),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(
        Sha256::hex(std::string(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Incremental update must match one-shot hashing.
    {
        Sha256 h;
        h.update(std::string("abcdbcdecdefdefgefghfghighijhij"));
        h.update(std::string("kijkljklmklmnlmnomnopnopq"));
        CHECK_EQ(h.hexdigest(),
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    // Chunking: a buffer of 10000 bytes with 4096-byte chunks -> 3 chunks.
    {
        std::string buf(10000, 'x');
        auto chunks = chunk_buffer(buf, 4096);
        CHECK_EQ(chunks.size(), static_cast<size_t>(3));
        CHECK_EQ(chunks[0].data.size(), static_cast<size_t>(4096));
        CHECK_EQ(chunks[2].data.size(), static_cast<size_t>(10000 - 8192));
        // Identical content chunks have identical hashes.
        CHECK_EQ(chunks[0].hash, chunks[1].hash);
    }

    // Content store dedup: writing the same content twice stores it once.
    {
        std::string root = svtest::temp_dir("hashchunk");
        ContentStore store(root);
        store.init();
        std::string data = "hello content addressed storage";
        std::string hash = Sha256::hex(data);

        bool first = store.put(hash, data);
        bool second = store.put(hash, data);
        CHECK(first);          // newly written
        CHECK(!second);        // deduplicated
        CHECK(store.has(hash));
        CHECK_EQ(store.get(hash), data);
    }

    return svtest::summary("hash_chunk");
}
