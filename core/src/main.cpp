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
    "  svcore drop     <name>       [--store DIR]\n"
    "  svcore gc       [--dry-run]  [--store DIR]\n"
    "\n"
    "options:\n"
    "  --store DIR   content store root (default: ./svstore)\n"
    "  --chunk N     chunk size in bytes for snapshot (default: 4096)\n"
    "  --dry-run     list gc candidates without deleting anything\n";

struct Args {
    std::string store = "svstore";
    size_t chunk = kDefaultChunkSize;
    bool dry_run = false;
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
        } else if (arg == "--dry-run") {
            a.dry_run = true;
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

int cmd_drop(const Args& a) {
    if (a.positional.empty()) {
        std::cerr << "drop requires <name>\n";
        return 2;
    }
    Engine eng(a.store, a.chunk);
    eng.drop(a.positional[0]);
    std::cout << "dropped snapshot '" << a.positional[0]
              << "' (run gc to reclaim unreferenced chunks)\n";
    return 0;
}

int cmd_gc(const Args& a) {
    Engine eng(a.store, a.chunk);
    GcStats s = eng.gc(a.dry_run);
    std::cout << (a.dry_run ? "gc (dry run)\n" : "gc\n")
              << "  chunks scanned : " << s.scanned << "\n"
              << "  referenced     : " << s.referenced << "\n"
              << "  candidates     : " << s.candidates.size() << "\n"
              << (a.dry_run ? "  would reclaim  : " : "  reclaimed      : ")
              << s.bytes_reclaimed << " bytes\n";
    if (a.dry_run) {
        for (const std::string& h : s.candidates) {
            std::cout << "  candidate " << h << "\n";
        }
    }
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
        if (sub == "drop") return cmd_drop(a);
        if (sub == "gc") return cmd_gc(a);
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
