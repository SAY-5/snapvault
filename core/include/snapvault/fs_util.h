#ifndef SNAPVAULT_FS_UTIL_H
#define SNAPVAULT_FS_UTIL_H

#include <cstdint>
#include <string>
#include <vector>

namespace snapvault {
namespace fsutil {

// Create a directory and any missing parents. No-op if it already exists.
void make_dirs(const std::string& path);

// True if the path exists (file or directory).
bool exists(const std::string& path);

// True if the path is a directory.
bool is_dir(const std::string& path);

// Read an entire file into a string. Throws std::runtime_error on failure.
std::string read_file(const std::string& path);

// Write a string to a file, creating parent directories as needed.
void write_file(const std::string& path, const std::string& data);

// Delete a file. Throws std::runtime_error if it cannot be removed.
void remove_file(const std::string& path);

// Size of a file in bytes. Throws std::runtime_error if it does not exist.
uint64_t file_size(const std::string& path);

// Recursively list regular files under root, returning paths relative to root
// (using '/' separators). Sorted for determinism.
std::vector<std::string> list_files(const std::string& root);

// Join two path components with a single '/'.
std::string join(const std::string& a, const std::string& b);

}  // namespace fsutil
}  // namespace snapvault

#endif  // SNAPVAULT_FS_UTIL_H
