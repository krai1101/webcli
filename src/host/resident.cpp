#include "host/resident.hpp"

#include <uv.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

#include "cli/output/format.hpp"
#include "core/errors.hpp"
#include "host/instance_worker.hpp"
#include "runtime/page/browsing_instance.hpp"
#include "storage/profile_store.hpp"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sddl.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace webcli {
namespace {
using Clock = std::chrono::steady_clock;
std::string executable_path() {
  std::vector<char> buffer(32768);
  size_t size = buffer.size();
  if (uv_exepath(buffer.data(), &size) < 0)
    throw CliError("EXECUTABLE_PATH", "cannot locate WebCLI executable");
  return std::string(buffer.data(), size);
}
std::string hash_string(const std::string& input) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : input) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}
#ifdef _WIN32
std::vector<unsigned char> token_user(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> value(size);
  if (!size || !GetTokenInformation(token, TokenUser, value.data(), size, &size)) value.clear();
  CloseHandle(token);
  return value;
}
std::string user_identity() {
  auto info = token_user(GetCurrentProcess());
  if (info.empty()) throw CliError("IPC_SECURITY", "cannot identify current user");
  LPSTR sid = nullptr;
  if (!ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid, &sid))
    throw CliError("IPC_SECURITY", "cannot encode user SID");
  std::string result(sid);
  LocalFree(sid);
  return result;
}
bool same_user_peer(uv_pipe_t* pipe, bool server) {
  uv_os_fd_t fd{};
  if (uv_fileno(reinterpret_cast<uv_handle_t*>(pipe), &fd) < 0) return false;
  ULONG pid = 0;
  const auto ok =
      server ? GetNamedPipeClientProcessId(fd, &pid) : GetNamedPipeServerProcessId(fd, &pid);
  if (!ok) return false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return false;
  auto peer = token_user(process), self = token_user(GetCurrentProcess());
  CloseHandle(process);
  return !peer.empty() && !self.empty() &&
         EqualSid(reinterpret_cast<TOKEN_USER*>(peer.data())->User.Sid,
                  reinterpret_cast<TOKEN_USER*>(self.data())->User.Sid);
}
#else
std::string user_identity() { return std::to_string(getuid()); }
bool same_user_peer(uv_pipe_t*, bool) {
  return true;
}  // Unix socket directory is restricted to this uid.
#endif
std::string endpoint() {
  const auto executable = executable_path();
  // libc++ may use a 128-bit filesystem clock representation. Microseconds
  // fit the standard integer overloads while covering every platform's epoch.
  const auto modified = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::filesystem::last_write_time(executable).time_since_epoch())
                            .count();
  const auto build =
      std::to_string(std::filesystem::file_size(executable)) + ":" +
      std::to_string(modified);
  // A freshly built executable must not reconnect to a worker running old code.
  const auto id = hash_string(executable + ":" + build) + "-" + hash_string(user_identity()) + "-" +
                  hash_string(profile_directory().string());
#ifdef _WIN32
  return "\\\\.\\pipe\\webcli-v2-" + id;
#else
  auto dir = std::filesystem::temp_directory_path() / ("webcli-ipc-" + user_identity());
  // sockaddr_un paths are short, especially on macOS. Long TMPDIR values
  // must not make the worker endpoint unusable.
  if (dir.string().size() + id.size() + 1 >= 100)
    dir = std::filesystem::path("/tmp") / ("webcli-ipc-" + user_identity());
  std::filesystem::create_directories(dir);
  struct stat status{};
  if (lstat(dir.c_str(), &status) || !S_ISDIR(status.st_mode) || status.st_uid != getuid())
    throw CliError("IPC_SECURITY", "unsafe IPC directory");
  if (chmod(dir.c_str(), 0700)) throw CliError("IPC_SECURITY", "cannot protect IPC directory");
  return (dir / id).string();
#endif
}
void allocate(uv_handle_t*, size_t, uv_buf_t* buffer) {
  buffer->base = static_cast<char*>(std::malloc(65536));
  buffer->len = buffer->base ? 65536 : 0;
}
void close_handle(uv_handle_t* handle) {
  if (!uv_is_closing(handle)) uv_close(handle, nullptr);
}
void close_loop(uv_loop_t& loop) {
  uv_walk(&loop, [](uv_handle_t* handle, void*) { close_handle(handle); }, nullptr);
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);
}
struct Client {
  uv_loop_t loop{};
  uv_pipe_t pipe{};
  uv_connect_t connect{};
  uv_write_t write{};
  std::string outgoing, buffer, payload;
  bool done = false, written = false;
  int code = 0;
};
void client_done(Client& client, int code = 0) {
  client.code = code;
  client.done = true;
  uv_read_stop(reinterpret_cast<uv_stream_t*>(&client.pipe));
  close_handle(reinterpret_cast<uv_handle_t*>(&client.pipe));
}
void client_read(uv_stream_t* stream, ssize_t count, const uv_buf_t* buffer) {
  auto& client = *static_cast<Client*>(stream->data);
  try {
    if (count > 0) {
      client.buffer.append(buffer->base, static_cast<size_t>(count));
      if (client.buffer.size() > kMaxFrame + 4) throw CliError("IPC_LIMIT", "response too large");
      if (extract_frame(client.buffer, client.payload)) client_done(client);
    } else if (count < 0)
      client_done(client, static_cast<int>(count));
  } catch (...) {
    client_done(client, UV_EPROTO);
  }
  std::free(buffer->base);
}
void connected_impl(uv_connect_t* connect, int status) {
  auto& client = *static_cast<Client*>(connect->data);
  if (status < 0) {
    client_done(client, status);
    return;
  }
  if (!same_user_peer(&client.pipe, false)) {
    client_done(client, UV_EACCES);
    return;
  }
  auto* stream = reinterpret_cast<uv_stream_t*>(&client.pipe);
  if ((status = uv_read_start(stream, allocate, client_read)) < 0) {
    client_done(client, status);
    return;
  }
  client.write.data = &client;
  uv_buf_t buffer =
      uv_buf_init(client.outgoing.data(), static_cast<unsigned>(client.outgoing.size()));
  status = uv_write(&client.write, stream, &buffer, 1, [](uv_write_t* request, int result) {
    auto& c = *static_cast<Client*>(request->data);
    if (result < 0)
      client_done(c, result);
    else
      c.written = true;
  });
  if (status < 0) client_done(client, status);
}
void connected(uv_connect_t* connect, int status) noexcept {
  try {
    connected_impl(connect, status);
  } catch (...) {
    client_done(*static_cast<Client*>(connect->data), UV_ENOMEM);
  }
}
struct Exchange {
  int code;
  bool written;
  std::string payload;
};
Exchange exchange(const std::string& path, const Request& request) {
  Client client;
  client.outgoing = frame(encode_request(request));
  if (uv_loop_init(&client.loop) < 0) throw CliError("IPC_INIT", "cannot initialize client loop");
  if (uv_pipe_init(&client.loop, &client.pipe, 0) < 0) {
    uv_loop_close(&client.loop);
    throw CliError("IPC_INIT", "cannot initialize client pipe");
  }
  client.pipe.data = &client;
  client.connect.data = &client;
  uv_pipe_connect(&client.connect, &client.pipe, path.c_str(), connected);
  const auto deadline = Clock::now() + std::chrono::milliseconds(kBrowserCommandTimeoutMs);
  while (!client.done && Clock::now() < deadline) {
    uv_run(&client.loop, UV_RUN_NOWAIT);
    if (!client.done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!client.done) client_done(client, UV_ETIMEDOUT);
  close_loop(client.loop);
  return {client.code, client.written, std::move(client.payload)};
}
void spawn_worker() {
  uv_loop_t loop{};
  if (uv_loop_init(&loop) < 0) throw CliError("IPC_INIT", "cannot initialize startup loop");
  const auto executable = executable_path();
  char* argv[] = {const_cast<char*>(executable.c_str()), const_cast<char*>("--worker"), nullptr};
  uv_stdio_container_t stdio[3]{};
  for (auto& io : stdio) io.flags = UV_IGNORE;
  uv_process_options_t options{};
  options.file = executable.c_str();
  options.args = argv;
  options.stdio_count = 3;
  options.stdio = stdio;
  options.flags = UV_PROCESS_DETACHED;
#ifdef _WIN32
  options.flags |= UV_PROCESS_WINDOWS_HIDE;
#endif
  uv_process_t child{};
  const int status = uv_spawn(&loop, &child, &options);
  if (status == 0) {
    uv_unref(reinterpret_cast<uv_handle_t*>(&child));
    close_handle(reinterpret_cast<uv_handle_t*>(&child));
  }
  close_loop(loop);
  if (status < 0)
    throw CliError("WORKER_START_FAILED",
                   std::string("cannot start browser worker: ") + uv_strerror(status));
}
struct Server;
struct Peer {
  uv_pipe_t pipe{};
  uv_write_t write{};
  Server* server;
  std::string buffer, outgoing;
  bool handled = false;
  std::future<Response> pending;
  Clock::time_point opened = Clock::now();
};
struct Server {
  struct Instance {
    std::unique_ptr<InstanceWorker> page;
    Clock::time_point activity = Clock::now();
  };
  uv_loop_t loop{};
  uv_pipe_t listener{};
  std::shared_ptr<OriginStorage> storage = std::make_shared<OriginStorage>();
  std::map<std::string, Instance> instances;
  std::vector<Peer*> peers;
  std::vector<std::unique_ptr<InstanceWorker>> retiring;
  bool stopping = false;
  Clock::time_point activity = Clock::now();
  InstanceWorker& page(const std::string& instance) {
    auto found = instances.find(instance);
    if (found == instances.end()) {
      if (instances.size() >= 64)
        throw CliError("INSTANCE_LIMIT",
                       "64 live browsing instances are already open; close an unused instance");
      found =
          instances.emplace(instance, Instance{std::make_unique<InstanceWorker>(storage)}).first;
    }
    found->second.activity = Clock::now();
    return *found->second.page;
  }
};
void close_peer(Peer* peer) {
  auto* handle = reinterpret_cast<uv_handle_t*>(&peer->pipe);
  if (uv_is_closing(handle)) return;
  uv_close(handle, [](uv_handle_t* closed) {
    auto* p = static_cast<Peer*>(closed->data);
    auto& peers = p->server->peers;
    peers.erase(std::remove(peers.begin(), peers.end(), p), peers.end());
    delete p;
  });
}
void reply(Peer& peer, const Response& response) {
  peer.handled = true;
  peer.server->activity = Clock::now();
  uv_read_stop(reinterpret_cast<uv_stream_t*>(&peer.pipe));
  peer.outgoing = frame(encode_response(response));
  peer.write.data = &peer;
  uv_buf_t buffer = uv_buf_init(peer.outgoing.data(), static_cast<unsigned>(peer.outgoing.size()));
  const int result =
      uv_write(&peer.write, reinterpret_cast<uv_stream_t*>(&peer.pipe), &buffer, 1,
               [](uv_write_t* request, int) { close_peer(static_cast<Peer*>(request->data)); });
  if (result < 0) close_peer(&peer);
}
void peer_read_impl(uv_stream_t* stream, ssize_t count, const uv_buf_t* buffer) {
  auto& peer = *static_cast<Peer*>(stream->data);
  Request request;
  Response response;
  try {
    if (count < 0) {
      close_peer(&peer);
      std::free(buffer->base);
      return;
    }
    if (count == 0) {
      std::free(buffer->base);
      return;
    }
    peer.buffer.append(buffer->base, static_cast<size_t>(count));
    if (peer.buffer.size() > kMaxFrame + 4) throw CliError("IPC_LIMIT", "command too large");
    std::string payload;
    if (!extract_frame(peer.buffer, payload)) {
      std::free(buffer->base);
      return;
    }
    request = decode_request(payload);
    // A queued peer may finish reading after the last page closed. Do not
    // execute an action in a worker whose listener has already shut down.
    if (peer.server->stopping &&
        uv_is_closing(reinterpret_cast<uv_handle_t*>(&peer.server->listener)))
      throw CliError("WORKER_STOPPING", "browser worker is closing; retry the command", true);
    peer.server->stopping = false;
    peer.server->activity = Clock::now();
    std::ostringstream out;
    if (request.command == "help") {
      show_help(out);
    } else if (request.command == "close") {
      const auto found = peer.server->instances.find(request.instance);
      if (found == peer.server->instances.end()) {
        out << (request.json ? "{\"closed\":true}\n" : "Page closed.\n");
      } else {
        peer.pending = found->second.page->enqueue(request);
        peer.server->retiring.push_back(std::move(found->second.page));
        peer.server->instances.erase(found);
        peer.server->stopping = peer.server->instances.empty();
      }
    } else {
      peer.pending = peer.server->page(request.instance).enqueue(request);
    }
    if (peer.pending.valid()) {
      peer.handled = true;
      uv_read_stop(reinterpret_cast<uv_stream_t*>(&peer.pipe));
      std::free(buffer->base);
      return;
    }
    response.output = out.str();
  } catch (const CliError& error) {
    response.status = 1;
    response.rejected_before_execution = error.code == "WORKER_STOPPING";
    std::ostringstream out;
    render_error(out, request.json, error.code, error.what(), error.retryable);
    if (request.json)
      response.output = out.str();
    else
      response.error = out.str();
  } catch (const std::exception& error) {
    response.status = 1;
    std::ostringstream out;
    render_error(out, request.json, "INTERNAL_ERROR", error.what());
    if (request.json)
      response.output = out.str();
    else
      response.error = out.str();
  }
  try {
    reply(peer, response);
  } catch (...) {
    close_peer(&peer);
  }
  std::free(buffer->base);
}
void peer_read(uv_stream_t* stream, ssize_t count, const uv_buf_t* buffer) noexcept {
  try {
    peer_read_impl(stream, count, buffer);
  } catch (...) {
    // Any formatting/allocation failure while reporting an error closes the peer.
    std::free(buffer->base);
    close_peer(static_cast<Peer*>(stream->data));
  }
}
void accept_peer(uv_stream_t* stream, int status) noexcept {
  if (status < 0) return;
  auto& server = *static_cast<Server*>(stream->data);
  Peer* peer = nullptr;
  bool initialized = false;
  try {
    peer = new Peer;
    peer->server = &server;
    if (uv_pipe_init(&server.loop, &peer->pipe, 0) < 0) {
      delete peer;
      return;
    }
    initialized = true;
    peer->pipe.data = peer;
    server.peers.push_back(peer);
    if (uv_accept(stream, reinterpret_cast<uv_stream_t*>(&peer->pipe)) < 0 ||
        server.peers.size() > 16 || !same_user_peer(&peer->pipe, true)) {
      close_peer(peer);
      return;
    }
    if (uv_read_start(reinterpret_cast<uv_stream_t*>(&peer->pipe), allocate, peer_read) < 0)
      close_peer(peer);
  } catch (...) {
    if (initialized)
      close_peer(peer);
    else
      delete peer;
  }
}
}  // namespace
Response send_request(const Request& request) {
  validate_request(request);
  if (request.instance.empty()) throw CliError("INVALID_INSTANCE", "browsing instance is required");
  const auto path = endpoint();
  auto result = exchange(path, request);
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  bool spawned = false;
  do {
    if (result.code == 0) {
      auto response = decode_response(result.payload);
      if (!response.rejected_before_execution) return response;
      // The old worker explicitly rejected this request before enqueueing it.
      // Reconnect during shutdown; never replay an uncertain action.
      spawned = false;
    } else {
      if (result.written)
        throw CliError("ACTION_OUTCOME_UNKNOWN",
                       "browser worker disconnected after receiving the command; observe before retrying");
      if (result.code != UV_ENOENT && result.code != UV_ECONNREFUSED)
        throw CliError("IPC_CONNECT", uv_strerror(result.code), true);
      if (request.command == "close")
        return {0, request.json ? "{\"closed\":true}\n" : "Page already closed.\n", ""};
      if (!spawned) {
#ifndef _WIN32
        struct stat status{};
        if (lstat(path.c_str(), &status) == 0 && S_ISSOCK(status.st_mode) && status.st_uid == getuid())
          unlink(path.c_str());
#endif
        spawn_worker();
        spawned = true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    result = exchange(path, request);
  } while (Clock::now() < deadline);
  throw CliError("WORKER_START_FAILED",
                 "browser worker did not become available within ten seconds", true);
}
int serve_browser() {
  const auto log_directory = diagnostic_directory();
  std::filesystem::create_directories(log_directory);
  const auto log_path = (log_directory / "worker.log").string();
#ifdef _WIN32
  HANDLE log_handle = CreateFileW((log_directory / "worker.log").c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_handle != INVALID_HANDLE_VALUE) {
    const int descriptor =
        _open_osfhandle(reinterpret_cast<intptr_t>(log_handle), _O_APPEND | _O_TEXT);
    if (descriptor >= 0) {
      _dup2(descriptor, _fileno(stderr));
      _close(descriptor);
      SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
    } else
      CloseHandle(log_handle);
  }
#else
  if (!std::freopen(log_path.c_str(), "a", stderr))
    throw CliError("WORKER_LOG", "cannot open browser worker log");
#endif
  const auto path = endpoint();
  Server server;
  if (uv_loop_init(&server.loop) < 0) throw CliError("IPC_INIT", "cannot initialize server loop");
  if (uv_pipe_init(&server.loop, &server.listener, 0) < 0) {
    uv_loop_close(&server.loop);
    throw CliError("IPC_INIT", "cannot initialize server pipe");
  }
  server.listener.data = &server;
  int status = uv_pipe_bind(&server.listener, path.c_str());
  if (status == 0)
    status = uv_listen(reinterpret_cast<uv_stream_t*>(&server.listener), 16, accept_peer);
  if (status < 0) {
    close_loop(server.loop);
    if (status == UV_EADDRINUSE) return 0;
    throw CliError("WORKER_BIND", uv_strerror(status));
  }
  std::fprintf(stderr, "browser worker ready\n");
  std::fflush(stderr);
  while (!server.stopping || !server.peers.empty()) {
    uv_run(&server.loop, UV_RUN_NOWAIT);
    if (!server.stopping) {
      for (auto it = server.instances.begin(); it != server.instances.end();) {
        if (Clock::now() - it->second.activity > std::chrono::minutes(30)) {
          it->second.page->stop();
          server.retiring.push_back(std::move(it->second.page));
          it = server.instances.erase(it);
          continue;
        }
        ++it;
      }
      if (Clock::now() - server.activity > std::chrono::minutes(30)) server.stopping = true;
    }
    server.retiring.erase(std::remove_if(server.retiring.begin(), server.retiring.end(),
                                         [](const auto& worker) { return worker->finished(); }),
                          server.retiring.end());
    for (auto* peer : server.peers) {
      if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&peer->pipe))) continue;
      if (peer->pending.valid() &&
          peer->pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try {
          reply(*peer, peer->pending.get());
        } catch (...) {
          close_peer(peer);
        }
      }
    }
    if (server.stopping) close_handle(reinterpret_cast<uv_handle_t*>(&server.listener));
    for (auto* peer : server.peers)
      if (Clock::now() - peer->opened > std::chrono::milliseconds(kBrowserCommandTimeoutMs + 5000))
        close_peer(peer);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  close_loop(server.loop);
  // libuv unlinks the bound socket when closing the listener. Unlinking again
  // here could remove a replacement worker's socket after it has already bound.
  return 0;
}
}  // namespace webcli
