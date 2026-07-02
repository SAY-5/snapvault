#include "snapvault/fs_util.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace snapvault {
namespace fsutil {

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool is_dir(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

void make_dirs(const std::string& path) {
    if (path.empty()) return;
    std::string acc;
    size_t i = 0;
    if (path[0] == '/') {
        acc = "/";
        i = 1;
    }
    std::stringstream ss(path);
    std::string part;
    // Split manually to preserve leading slash handling.
    std::string remaining = path.substr(i);
    std::stringstream rs(remaining);
    while (std::getline(rs, part, '/')) {
        if (part.empty()) continue;
        acc = acc.empty() ? part : (acc == "/" ? "/" + part : acc + "/" + part);
        if (::mkdir(acc.c_str(), 0755) != 0) {
            if (errno != EEXIST) {
                throw std::runtime_error("mkdir failed for " + acc + ": " +
                                         std::strerror(errno));
            }
        }
    }
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& data) {
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        make_dirs(path.substr(0, pos));
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write file: " + path);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("write failed: " + path);
}

static void walk(const std::string& base, const std::string& rel,
                 std::vector<std::string>& out) {
    std::string dir = rel.empty() ? base : join(base, rel);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string child_rel = rel.empty() ? name : join(rel, name);
        std::string full = join(base, child_rel);
        if (is_dir(full)) {
            walk(base, child_rel, out);
        } else {
            out.push_back(child_rel);
        }
    }
    ::closedir(d);
}

std::vector<std::string> list_files(const std::string& root) {
    std::vector<std::string> out;
    walk(root, "", out);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace fsutil
}  // namespace snapvault
