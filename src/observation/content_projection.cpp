#include "observation/content_projection.hpp"

#include <string_view>
#include <unordered_map>

#include "core/text.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_runtime.hpp"

namespace webcli {
namespace {
std::string decode_fragment(std::string_view fragment) {
  const auto hex = [](char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  std::string result;
  for (std::size_t i = 0; i < fragment.size(); ++i) {
    if (fragment[i] == '%' && i + 2 < fragment.size()) {
      const int first = hex(fragment[i + 1]), second = hex(fragment[i + 2]);
      if (first >= 0 && second >= 0) {
        result += static_cast<char>(first * 16 + second);
        i += 2;
        continue;
      }
    }
    result += fragment[i];
  }
  return result;
}
}  // namespace
void project_content(lxb_dom_node_t* root, Page& page, const ContentProjection& projection) {
  auto& runtime = projection.runtime;
  const auto& visible_refs = projection.references;
  const auto& visible_labels = projection.labels;
  const auto& consumed_labels = projection.consumed_labels;
  const auto& ignored = projection.ignored;
  page.anchor_offsets.clear();
  std::size_t main_offset = 0;
  lxb_dom_node_t* main = nullptr;
  std::unordered_map<std::string, lxb_dom_node_t*> first_anchors;
  for (auto* node : projection.nodes) {
    auto* scope = node;
    while (scope->parent) scope = scope->parent;
    if (scope == root) {
      const auto id = attr(node, "id"), name = tag_name(node) == "a" ? attr(node, "name") : "";
      if (!id.empty()) first_anchors.emplace(id, node);
      if (!name.empty()) first_anchors.emplace(name, node);
    }
    if (!main && (tag_name(node) == "main" || attr(node, "role") == "main")) {
      bool hidden = false;
      for (auto* ancestor = node; ancestor; ancestor = runtime.composed_parent(ancestor))
        if (ignored(ancestor)) {
          hidden = true;
          break;
        }
      if (!hidden) main = node;
    }
  }
  std::string pending, pending_kind = "text";
  std::size_t bytes = 0;
  std::unordered_map<std::string, std::string> link_targets;
  for (const auto& link : page.links) link_targets.emplace(link.ref, link.url);
  auto emit = [&](std::string kind, std::string text, std::string reference = "") {
    if (kind != "code")
      text = trim(text);
    else {
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    }
    if (text.empty()) return;
    if (!page.content.empty()) {
      auto& previous = page.content.back();
      if (previous.text == text && previous.kind == "heading" && kind == "link") {
        previous.kind = kind;
        previous.ref = reference;
        for (auto& anchor : page.anchor_offsets)
          if (anchor.second == page.content.size()) anchor.second = page.content.size() - 1;
        if (main_offset == page.content.size()) --main_offset;
        return;
      }
      // Equal adjacent link labels can represent different destinations. Keep
      // both actions and distinguish their targets instead of dropping one.
      if (previous.text == text && previous.kind == "link" && kind == "link" &&
          link_targets.count(previous.ref) && link_targets.count(reference) &&
          link_targets.at(previous.ref) != link_targets.at(reference)) {
        auto destination = [&](const std::string& ref) {
          const auto& url = link_targets.at(ref);
          const auto start = url.find("://") + 3;
          return url.substr(start, url.find('/', start) - start);
        };
        auto first = destination(previous.ref), second = destination(reference);
        if (first == second) {
          first = truncate(link_targets.at(previous.ref), 100);
          second = truncate(link_targets.at(reference), 100);
        }
        if (bytes + text.size() + first.size() + second.size() + 6 <= kMaxTextChars) {
          previous.text += " (" + first + ")";
          bytes += first.size() + 3;
          text += " (" + second + ")";
        }
      }
    }
    if (page.content.size() >= 4000 || bytes + text.size() > kMaxTextChars) {
      page.truncated = true;
      return;
    }
    bytes += text.size();
    page.content.push_back({std::move(kind), std::move(text), std::move(reference)});
  };
  auto flush = [&]() {
    emit(pending_kind, pending);
    pending.clear();
  };
  std::function<void(lxb_dom_node_t*, std::string, int)> walk = [&](lxb_dom_node_t* node,
                                                                    std::string kind, int depth) {
    if (!node || depth > 256 || ignored(node) || projection.suppressed_links.count(node)) return;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
      const auto text = node_text(node);
      const auto remaining = kMaxTextChars - std::min(kMaxTextChars, bytes + pending.size());
      if (text.size() > remaining) page.truncated = true;
      pending += truncate(text, remaining);
      return;
    }
    const auto tag = tag_name(node);
    if (tag == "iframe") {
      flush();
      if (projection.frame)
        for (auto& block : projection.frame(node))
          emit(std::move(block.kind), std::move(block.text), std::move(block.ref));
      return;
    }
    const auto id = attr(node, "id"), name = tag == "a" ? attr(node, "name") : "";
    if (node == main || !id.empty() || !name.empty()) {
      flush();
      if (node == main) main_offset = page.content.size();
      for (const auto& anchor : {id, name})
        if (!anchor.empty() && first_anchors.count(anchor) && first_anchors.at(anchor) == node)
          page.anchor_offsets.emplace(anchor, page.content.size());
    }
    if (tag == "pre") {
      flush();
      std::string code;
      const auto code_budget = kMaxTextChars - std::min(kMaxTextChars, bytes);
      std::vector<lxb_dom_node_t*> code_actions;
      std::function<void(lxb_dom_node_t*, int)> collect_code = [&](lxb_dom_node_t* child,
                                                                   int level) {
        if (!child) return;
        if (level > 256 || code.size() >= code_budget) {
          page.truncated = true;
          return;
        }
        if (ignored(child)) return;
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
          const auto text = node_text(child);
          const auto remaining = code_budget - code.size();
          if (text.size() > remaining) page.truncated = true;
          code += truncate(text, remaining);
        } else {
          if (visible_refs.count(child)) {
            code_actions.push_back(child);
            if (visible_refs.at(child)[0] != 'L') return;
          }
          for (auto* nested : runtime.composed_children(child)) collect_code(nested, level + 1);
        }
      };
      collect_code(node, depth);
      emit("code", std::move(code));
      for (auto* action : code_actions)
        emit(visible_refs.at(action)[0] == 'L'   ? "link"
             : visible_refs.at(action)[0] == 'B' ? "button"
                                                 : "field",
             visible_labels.at(action), visible_refs.at(action));
      return;
    }
    if (visible_refs.count(node)) {
      flush();
      emit(visible_refs.at(node)[0] == 'L'   ? "link"
           : visible_refs.at(node)[0] == 'B' ? "button"
                                             : "field",
           visible_labels.at(node), visible_refs.at(node));
      return;
    }
    if (consumed_labels.count(node)) {
      std::function<void(lxb_dom_node_t*, int)> controls = [&](lxb_dom_node_t* child, int level) {
        if (!child || level > 256 || ignored(child)) return;
        if (visible_refs.count(child)) {
          walk(child, kind, depth + level);
          return;
        }
        for (auto* nested = child->first_child; nested; nested = nested->next)
          controls(nested, level + 1);
      };
      for (auto* child = node->first_child; child; child = child->next) controls(child, 1);
      return;
    }
    if (tag == "input" || tag == "select" || tag == "button" || tag == "textarea") return;
    const bool heading = tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
    const bool block = heading || tag == "p" || tag == "div" || tag == "li" || tag == "tr" ||
                       tag == "section" || tag == "article" || tag == "nav";
    const auto inherited_kind = kind;
    if (block) {
      flush();
      if (heading)
        kind = "heading";
      else if (tag == "li")
        kind = "list-item";
      else if (tag == "tr")
        kind = "table-row";
      else if (tag == "nav")
        kind = "navigation";
      pending_kind = kind;
    }
    if (tag == "td" || tag == "th") {
      pending = trim(pending);
      if (!pending.empty()) pending += " | ";
    }
    for (auto* child : runtime.composed_children(node)) walk(child, kind, depth + 1);
    if (block || tag == "br") flush();
    if (block) pending_kind = inherited_kind;
  };
  walk(root, "text", 0);
  flush();
  page.observation_offset = main_offset < page.content.size() ? main_offset : 0;
  const auto hash = page.url.find('#');
  if (hash != std::string::npos) {
    const auto fragment = decode_fragment(page.url.substr(hash + 1));
    const auto anchor = page.anchor_offsets.find(fragment);
    if (anchor != page.anchor_offsets.end() && anchor->second < page.content.size())
      page.observation_offset = anchor->second;
  }
  for (const auto& block : page.content) {
    if (!page.text.empty()) page.text += '\n';
    page.text += block.text;
  }
}
}  // namespace webcli
