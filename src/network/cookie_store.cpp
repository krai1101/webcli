#include "network/cookie_store.hpp"

#include <array>
#include <mutex>

#include "core/errors.hpp"
#include "network/public_suffix.hpp"
#include "storage/profile_store.hpp"

namespace webcli {
struct CookieStore::Impl {
  CURLSH* share = nullptr;
  CURL* keeper = nullptr;
  std::array<std::recursive_mutex, CURL_LOCK_DATA_LAST> locks;
  std::recursive_mutex persistence;

  static void lock(CURL*, curl_lock_data data, curl_lock_access, void* opaque) noexcept {
    static_cast<Impl*>(opaque)->locks[static_cast<std::size_t>(data)].lock();
  }
  static void unlock(CURL*, curl_lock_data data, void* opaque) noexcept {
    static_cast<Impl*>(opaque)->locks[static_cast<std::size_t>(data)].unlock();
  }
  ~Impl() {
    if (keeper) curl_easy_cleanup(keeper);
    if (share) curl_share_cleanup(share);
  }
};

CookieStore::CookieStore() : impl_(std::make_unique<Impl>()) {
  const auto* version = curl_version_info(CURLVERSION_NOW);
  if (!version || !(version->features & CURL_VERSION_PSL))
    throw CliError("PSL_UNAVAILABLE", "libcurl must be built with public suffix support");
  // Both server-set and JavaScript cookies require suffix validation. Fail
  // closed if a replacement dependency was built without its embedded data.
  public_suffix("com");
  auto& state = *impl_;
  state.share = curl_share_init();
  state.keeper = curl_easy_init();
  if (!state.share || !state.keeper)
    throw CliError("COOKIE_INIT", "cannot initialize profile cookies");
  if (curl_share_setopt(state.share, CURLSHOPT_LOCKFUNC, Impl::lock) != CURLSHE_OK ||
      curl_share_setopt(state.share, CURLSHOPT_UNLOCKFUNC, Impl::unlock) != CURLSHE_OK ||
      curl_share_setopt(state.share, CURLSHOPT_USERDATA, &state) != CURLSHE_OK ||
      curl_share_setopt(state.share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK)
    throw CliError("COOKIE_INIT", "cannot share the profile cookie engine");
  auto path = profile_directory() / "cookies.txt";
  std::filesystem::create_directories(path.parent_path());
  const auto filename = path.string();
  attach(state.keeper);
  if (curl_easy_setopt(state.keeper, CURLOPT_COOKIEFILE, filename.c_str()) != CURLE_OK ||
      curl_easy_setopt(state.keeper, CURLOPT_COOKIEJAR, filename.c_str()) != CURLE_OK ||
      curl_easy_setopt(state.keeper, CURLOPT_COOKIELIST, "RELOAD") != CURLE_OK)
    throw CliError("COOKIE_INIT", "cannot load profile cookies");
}
CookieStore::~CookieStore() = default;
void CookieStore::attach(CURL* curl) {
  if (curl_easy_setopt(curl, CURLOPT_SHARE, impl_->share) != CURLE_OK ||
      curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "") != CURLE_OK)
    throw CliError("COOKIE_INIT", "cannot attach profile cookies");
}
void CookieStore::persist() {
  // File writes serialize, but requests stay concurrent. libcurl protects its
  // shared cookie list using the short lock callbacks above.
  std::lock_guard<std::recursive_mutex> lock(impl_->persistence);
  if (curl_easy_setopt(impl_->keeper, CURLOPT_COOKIELIST, "FLUSH") != CURLE_OK)
    throw CliError("COOKIE_WRITE", "cannot persist profile cookies");
}
std::vector<std::string> CookieStore::snapshot() {
  std::lock_guard<std::recursive_mutex> lock(impl_->persistence);
  curl_slist* raw = nullptr;
  if (curl_easy_getinfo(impl_->keeper, CURLINFO_COOKIELIST, &raw) != CURLE_OK)
    throw CliError("COOKIE_READ", "cannot read profile cookies");
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> owner(raw, curl_slist_free_all);
  std::vector<std::string> result;
  for (auto* item = raw; item; item = item->next) result.emplace_back(item->data);
  return result;
}
void CookieStore::set_document_cookie(const std::string& url, const std::string& value) {
  // Keep validation and insertion atomic with HTTP cookie updates. The order
  // matches persist/snapshot; curl's share callbacks reenter the cookie lock.
  std::lock_guard<std::recursive_mutex> persist_lock(impl_->persistence);
  std::lock_guard<std::recursive_mutex> cookie_lock(impl_->locks[CURL_LOCK_DATA_COOKIE]);
  write_document_cookie(*this, url, value);
}
void CookieStore::import_cookie(const std::string& line) {
  std::lock_guard<std::recursive_mutex> lock(impl_->persistence);
  if (curl_easy_setopt(impl_->keeper, CURLOPT_COOKIELIST, line.c_str()) != CURLE_OK ||
      curl_easy_setopt(impl_->keeper, CURLOPT_COOKIELIST, "FLUSH") != CURLE_OK)
    throw CliError("COOKIE_WRITE", "cannot store document cookie");
}
namespace {
std::mutex registry_mutex;
std::shared_ptr<CookieStore> store;
}  // namespace
std::shared_ptr<CookieStore> profile_cookies() {
  std::lock_guard<std::mutex> lock(registry_mutex);
  if (!store) store = std::make_shared<CookieStore>();
  return store;
}
void shutdown_cookie_store() noexcept { store.reset(); }
}  // namespace webcli
