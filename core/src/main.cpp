#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "snapvault/engine.h"

namespace {

using namespace snapvault;

const char* kUsage =
    "svcore - content-addressed storage engine for snapvault\n"
    "\n"
    "usage:\n"
    "  svcore snapshot <name> <dir> [--store DIR] [--chunk N]\n"
    "  svcore restore  <name> <dir> [--store DIR]\n"
    "  svcore verify   [name]       [--store DIR]\n"
    "\n"
    "options:\n"
    "  --store DIR   content store root (default: ./svstore)\n"
    "  --chunk N     chunk size in bytes for snapshot (default: 4096)\n";

struct Args {
    std::string store = "svstore";
    size_t chunk = kDefaultChunkSize;
    std::vector<std::string> positional;
};

Args parse(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--store" && i + 1 < argc) {
            a.store = argv[++i];
        } else if (arg == "--chunk" && i + 1 < argc) {
            a.chunk = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else {
            a.positional.push_back(arg);
        }
    }
    return a;
}

int cmd_snapshot(const Args& a) {
    if (a.positional.size() < 2) {
        std::cerr << "snapshot requires <name> <dir>\n";
        return 2;
    }
    Engine eng(a.store, a.chunk);
    SnapshotStats s = eng.snapshot(a.positional[0], a.positional[1]);
    std::cout << "snapshot '" << a.positional[0] << "' created\n"
              << "  files        : " << s.files << "\n"
              << "  bytes        : " << s.bytes << "\n"
              << "  chunk refs   : " << s.chunks << "\n"
              << "  new chunks   : " << s.new_chunks << "\n"
              << "  deduped      : " << s.deduped_chunks << "\n";
    return 0;
}

int cmd_restore(const Args& a) {
    if (a.positional.size() < 2) {
        std::cerr << "restore requires <name> <dir>\n";
        return 2;
    }
    Engine eng(a.store, a.chunk);
    eng.restore(a.positional[0], a.positional[1]);
    std::cout << "restored snapshot '" << a.positional[0] << "' to "
              << a.positional[1] << "\n";
    return 0;
}

int cmd_verify(const Args& a) {
    Engine eng(a.store, a.chunk);
    std::string name = a.positional.empty() ? "" : a.positional[0];
    VerifyResult r = eng.verify(name);
    std::cout << "verify " << (name.empty() ? "all snapshots" : name) << "\n"
              << "  checked : " << r.checked << "\n"
              << "  bad     : " << r.bad << "\n";
    if (!r.ok()) {
        std::cout << "  first bad hash: " << r.first_bad_hash << "\n";
        std::cerr << "verify FAILED\n";
        return 1;
    }
    std::cout << "verify OK\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << kUsage;
        return 2;
    }
    std::string sub = argv[1];
    Args a = parse(argc, argv, 2);
    try {
        if (sub == "snapshot") return cmd_snapshot(a);
        if (sub == "restore") return cmd_restore(a);
        if (sub == "verify") return cmd_verify(a);
        if (sub == "-h" || sub == "--help" || sub == "help") {
            std::cout << kUsage;
            return 0;
        }
        std::cerr << "unknown command: " << sub << "\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
