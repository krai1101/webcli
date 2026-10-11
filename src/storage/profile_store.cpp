#include "storage/profile_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/errors.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace webcli {
namespace {
std::filesystem::path data_directory() {
#ifdef _WIN32
  const auto root = std::filesystem::temp_directory_path() / "webcli";
#else
  const auto root = std::filesystem::temp_directory_path() / ("webcli-" + std::to_string(getuid()));
  if (mkdir(root.c_str(), 0700) != 0 && errno != EEXIST)
    throw CliError("PROFILE_DIRECTORY", "cannot create private profile directory");
  struct stat status{};
  if (lstat(root.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != getuid())
    throw CliError("IPC_SECURITY", "unsafe profile directory");
  if (chmod(root.c_str(), 0700) != 0)
    throw CliError("IPC_SECURITY", "cannot protect profile directory");
#endif
  return root;
}
}  // namespace
std::filesystem::path profile_directory() { return data_directory() / "profile"; }
std::filesystem::path diagnostic_directory() { return data_directory() / "logs"; }

namespace {
std::filesystem::path storage_path(const std::string& origin) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : origin) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream name;
  name << std::hex << hash << ".storage";
  return profile_directory() / name.str();
}
void write_string(std::ostream& stream, const std::string& value) {
  stream << value.size() << '\n';
  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
}
std::string read_string(std::istream& stream) {
  std::string count;
  if (!std::getline(stream, count) || count.size() > 8 || count.empty() ||
      !std::all_of(count.begin(), count.end(),
                   [](unsigned char c) { return c >= '0' && c <= '9'; }))
    throw CliError("STORAGE_CORRUPT", "invalid localStorage file");
  auto size = std::stoul(count);
  if (size > 1024 * 1024) throw CliError("STORAGE_CORRUPT", "localStorage file exceeds quota");
  std::string value(size, '\0');
  stream.read(value.data(), static_cast<std::streamsize>(size));
  if (!stream) throw CliError("STORAGE_CORRUPT", "incomplete localStorage file");
  return value;
}
}  // namespace
std::map<std::string, std::string>& StorageStore::data(const std::string& origin, bool persistent) {
  if (!persistent) return transient_.values[origin];
  auto found = local_->values.find(origin);
  if (found != local_->values.end()) return found->second;
  std::map<std::string, std::string> values;
  const auto path = storage_path(origin);
  std::ifstream file(path, std::ios::binary);
  if (!file && std::filesystem::exists(path))
    throw CliError("STORAGE_READ_FAILED", "could not read localStorage file");
  if (file) {
    if (read_string(file) != origin)
      throw CliError("STORAGE_CORRUPT", "localStorage origin mismatch");
    std::size_t total = 0;
    while (file.peek() != std::char_traits<char>::eof()) {
      auto key = read_string(file);
      auto value = read_string(file);
      total += key.size() + value.size();
      if (values.size() >= 1024 || total > 1024 * 1024)
        throw CliError("STORAGE_CORRUPT", "localStorage exceeds quota");
      if (!values.emplace(std::move(key), std::move(value)).second)
        throw CliError("STORAGE_CORRUPT", "duplicate localStorage key");
    }
  }
  // Values are persisted on every mutation. Native bindings borrow this map
  // only for one synchronous call, so cached origins can be safely evicted.
  if (local_->values.size() >= 64) local_->values.erase(local_->values.begin());
  return local_->values.emplace(origin, std::move(values)).first->second;
}
void StorageStore::save(const std::string& origin) {
  auto target = storage_path(origin), temp = target;
  temp += ".tmp";
  std::filesystem::create_directories(target.parent_path());
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    write_string(out, origin);
    for (const auto& pair : local_->values.at(origin)) {
      write_string(out, pair.first);
      write_string(out, pair.second);
    }
    out.flush();
    if (!out) throw CliError("STORAGE_WRITE_FAILED", "could not persist localStorage");
  }
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw CliError("STORAGE_WRITE_FAILED", "could not replace localStorage file");
#else
  std::filesystem::rename(temp, target);
#endif
}
}  // namespace webcli
