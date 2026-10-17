#include "cli/installation.hpp"

#include <uv.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cli/installation_scripts.hpp"
#include "core/errors.hpp"

namespace webcli {
namespace {
namespace fs = std::filesystem;
struct WorkDirectory {
  fs::path path;
  bool retained = false;
  ~WorkDirectory() {
    std::error_code error;
    if (!retained && !path.empty()) fs::remove_all(path, error);
  }
};
template <std::size_t N>
void write_script(const fs::path& path, const unsigned char (&bytes)[N]) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes), N);
  if (!file) throw CliError("INSTALL_FAILED", "cannot write installation helper");
}
struct Child {
  uv_process_t process{};
  int status = 1;
};
int run(std::vector<std::string>& arguments) {
  uv_loop_t loop;
  if (uv_loop_init(&loop) < 0) throw CliError("INSTALL_FAILED", "cannot initialize helper");
  Child child;
  child.process.data = &child;
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);
  uv_stdio_container_t stdio[3]{};
  for (int i = 0; i < 3; ++i) {
    stdio[i].flags = UV_INHERIT_FD;
    stdio[i].data.fd = i;
  }
  uv_process_options_t options{};
  options.file = argv[0];
  options.args = argv.data();
  options.stdio_count = 3;
  options.stdio = stdio;
#ifdef _WIN32
  options.flags = UV_PROCESS_WINDOWS_HIDE;
#endif
  options.exit_cb = [](uv_process_t* process, int64_t status, int signal) {
    static_cast<Child*>(process->data)->status =
        signal || status < 0 || status > 255 ? 1 : static_cast<int>(status);
    uv_close(reinterpret_cast<uv_handle_t*>(process), nullptr);
  };
  const int result = uv_spawn(&loop, &child.process, &options);
  if (result < 0) {
    uv_loop_close(&loop);
    throw CliError("INSTALL_FAILED",
                   std::string("cannot start installation helper: ") + uv_strerror(result));
  }
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);
  return child.status;
}
}  // namespace
bool installation_command(const Request& request) {
  return request.command == "update" || request.command == "uninstall";
}
int manage_installation(const Request& request) {
  if (request.json || request.debug || request.offset_set || !request.document.empty())
    throw CliError("INVALID_ARGUMENT", "installation commands do not accept browsing options");
  std::vector<char> executable(32768);
  std::size_t size = executable.size();
  if (uv_exepath(executable.data(), &size) < 0)
    throw CliError("EXECUTABLE_PATH", "cannot locate installed executable");
  const auto current = fs::canonical(fs::u8path(std::string(executable.data(), size))).u8string();
  const auto temp = fs::absolute(fs::temp_directory_path()).lexically_normal();
  const auto pattern = (temp / "webcli-manage-XXXXXX").u8string();
  uv_fs_t operation{};
  const int result = uv_fs_mkdtemp(nullptr, &operation, pattern.c_str(), nullptr);
  const auto path = result < 0 ? fs::path{} : fs::u8path(operation.path);
  uv_fs_req_cleanup(&operation);
  if (result < 0) throw CliError("INSTALL_FAILED", "cannot create private helper directory");
  if (path.parent_path().lexically_normal() != temp ||
      path.filename().u8string().rfind("webcli-manage-", 0) != 0)
    throw CliError("INSTALL_FAILED", "unexpected helper directory");
  WorkDirectory work{path};
#ifdef _WIN32
  const auto installer = path / "install.ps1", helper = path / "manage.ps1";
  std::vector<std::string> args{"powershell.exe",
                                "-NoLogo",
                                "-NoProfile",
                                "-NonInteractive",
                                "-ExecutionPolicy",
                                "Bypass",
                                "-File",
                                helper.u8string(),
                                "-Operation",
                                request.command,
                                "-Executable",
                                current,
                                "-ParentId",
                                std::to_string(uv_os_getpid())};
#else
  const auto installer = path / "install.sh", helper = path / "manage.sh";
  std::vector<std::string> args{"/bin/bash", helper.u8string(), request.command, current};
#endif
  write_script(installer, kInstallerScript);
  write_script(helper, kMaintenanceScript);
  const auto status = run(args);
#ifdef _WIN32
  // The removal helper waits for this executable to exit, then removes its
  // own temporary directory. Updates use immutable version directories.
  work.retained = status == 0 && request.command == "uninstall";
#endif
  return status;
}
}  // namespace webcli
