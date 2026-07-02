#ifndef SNAPVAULT_TEST_UTIL_H
#define SNAPVAULT_TEST_UTIL_H

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// Minimal assertion helpers so tests need no external framework.
namespace svtest {

inline int& failures() {
    static int f = 0;
    return f;
}

inline void check(bool cond, const std::string& msg, const char* file, int line) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, msg.c_str());
        ++failures();
    }
}

inline int summary(const char* name) {
    if (failures() == 0) {
        std::printf("PASS %s\n", name);
        return 0;
    }
    std::fprintf(stderr, "%d assertion(s) failed in %s\n", failures(), name);
    return 1;
}

// Create a unique temporary directory path (not created on disk yet).
inline std::string temp_dir(const std::string& tag) {
    const char* base = std::getenv("TMPDIR");
    std::string root = base ? base : "/tmp";
    if (!root.empty() && root.back() == '/') root.pop_back();
    return root + "/svtest_" + tag + "_" + std::to_string(::getpid());
}

}  // namespace svtest

#define CHECK(cond) ::svtest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) \
    ::svtest::check((a) == (b), #a " == " #b, __FILE__, __LINE__)

#endif  // SNAPVAULT_TEST_UTIL_H
