#include "runtime/css/stylesheet_loader.hpp"

#include <algorithm>
#include <future>
#include <map>
#include <sstream>
#include <vector>

#include "core/errors.hpp"
#include "network/http_client.hpp"
#include "network/referrer.hpp"
#include "network/resource_service.hpp"
#include "runtime/css/visibility_rules.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/network/referrer_policy.hpp"
namespace webcli {
LoadedStylesheets load_visibility_stylesheets(lxb_dom_node_t* root, const std::string& base,
                                              Page& page, const LoadedStylesheets* previous,
                                              const std::string& referrer_policy,
                                              std::shared_ptr<std::atomic<bool>> cancelled) {
  LoadedStylesheets result;
  struct Job {
    lxb_dom_node_t* node;
    std::string href, url;
  };
  std::vector<Job> jobs;
  std::map<std::string, HttpRequest> requests;
  std::vector<std::pair<lxb_dom_node_t*, unsigned>> pending{{root, 0}};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kStylesheetLoadTimeoutMs);
  auto warning = [&](const std::string& message) {
    if (page.script_errors.size() < kMaxScriptErrors)
      page.script_errors.push_back("stylesheet visibility: " + message);
  };
  unsigned visited = 0;
  while (!pending.empty() && ++visited <= 50000) {
    const auto [node, depth] = pending.back();
    pending.pop_back();
    if (!node || depth > 256) continue;
    if (tag_name(node) == "link" && !has_attr(node, "disabled") &&
        visibility_media_matches(attr(node, "media"))) {
      std::istringstream rel(attr(node, "rel"));
      std::string token;
      bool sheet = false, alternate = false;
      while (rel >> token) {
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        sheet |= token == "stylesheet";
        alternate |= token == "alternate";
      }
      const auto href = attr(node, "href");
      if (sheet && !alternate && !href.empty()) {
        if (previous) {
          const auto cached = previous->find(node);
          if (cached != previous->end() && cached->second.href == href) {
            result.emplace(node, cached->second);
            continue;
          }
        }
        const auto url = resolve_web_url(base, href);
        const auto policy = element_referrer_policy(referrer_policy, node);
        const auto key = url ? *url + '\n' + request_referrer(base, *url, policy) : "";
        if (!url)
          warning("invalid stylesheet URL: " + href);
        else if (requests.count(key))
          jobs.push_back({node, href, key});
        else if (requests.size() + result.size() >= kStylesheetRequests)
          warning("stylesheet request quota exceeded: " + href);
        else {
          HttpRequest request;
          request.metadata_mode = "no-cors";
          request.destination = "style";
          request.owner_cancelled = cancelled;
          request.url = *url;
          request.cookie_origin = base;
          request.referrer_source = base;
          request.referrer_policy = policy;
          request.max_body_bytes = kStylesheetBodyBytes;
          request.timeout_ms = static_cast<long>(
              std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(
                                            deadline - std::chrono::steady_clock::now())
                                            .count()));
          requests.emplace(key, std::move(request));
          jobs.push_back({node, href, key});
        }
      }
    }
    for (auto* child = node->last_child; child; child = child->prev)
      pending.emplace_back(child, depth + 1);
  }
  std::size_t bytes = 0;
  for (const auto& entry : result)
    if (entry.second.source)
      bytes = std::min(kVisibilityStylesheetBytes, bytes + entry.second.source->size());
  std::map<std::string, std::shared_ptr<const std::string>> sources;
  // Launch bounded waves rather than one thread per stylesheet. Each response
  // is reduced and released before starting the next wave; only visibility
  // declarations are retained. All diagnostics and DOM access stay on the owner.
  auto next = requests.begin();
  while (next != requests.end()) {
    std::vector<std::pair<std::string, std::future<HttpResponse>>> active;
    for (; next != requests.end() && active.size() < kResourceConcurrency; ++next) {
      const auto& url = next->first;
      auto request = next->second;
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - std::chrono::steady_clock::now())
                                 .count();
      if (remaining <= 0) {
        sources.emplace(url, nullptr);
        warning(url + ": stylesheet acquisition deadline expired");
        continue;
      }
      request.timeout_ms = static_cast<long>(remaining);
      request.deadline_start = std::chrono::steady_clock::now();
      try {
        active.emplace_back(url, resource_service().submit(std::move(request)));
      } catch (const std::exception& error) {
        sources.emplace(url, nullptr);
        warning(url + ": " + error.what());
      }
    }
    for (auto& transfer : active) {
      try {
        auto response = transfer.second.get();
        if (response.status < 200 || response.status >= 300)
          throw CliError("CSS_HTTP_STATUS", "HTTP " + std::to_string(response.status));
        if (response.content_type.rfind("text/css", 0) != 0)
          throw CliError("CSS_CONTENT_TYPE", "expected text/css");
        bool truncated = false;
        auto source = visibility_css_source(response.body, truncated);
        if (truncated) warning(transfer.first + ": visibility rule quota exceeded");
        if (source.size() > kVisibilityStylesheetBytes - bytes)
          throw CliError("CSS_QUOTA", "stylesheet sources exceed 2 MiB");
        bytes += source.size();
        sources.emplace(transfer.first, std::make_shared<const std::string>(std::move(source)));
      } catch (const std::exception& error) {
        sources.emplace(transfer.first, nullptr);
        warning(transfer.first + ": " + error.what());
      }
    }
  }
  for (const auto& job : jobs) {
    const auto& source = sources.at(job.url);
    result.emplace(job.node, LoadedStylesheet{job.href, source});
  }
  return result;
}
}  // namespace webcli
