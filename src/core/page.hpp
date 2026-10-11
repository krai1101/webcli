#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/resource_limits.hpp"

namespace webcli {

struct Link {
  std::string label, url, selector, ref;
  int node_id = -1;
  int frame_id = 0;  // Internal action routing; references remain L/B/I/S.
};
struct Button {
  std::string label, type, selector, ref;
  int node_id = -1;
  std::string role = "button";
  bool selected = false;
  int frame_id = 0;
};
struct Field {
  std::string id, name, label, type, value, selector, ref;
  bool disabled = false, readonly = false, checked = false, required = false;
  bool has_selected_value = true;  // Internal form submission eligibility.
  std::vector<std::string> options;
  int node_id = -1, form_index = -1;
  int frame_id = 0;
};
struct Form {
  std::string action, method, selector;
  std::vector<Field> fields;
  int node_id = -1;
  int frame_id = 0;
};
struct ContentBlock {
  std::string kind, text, ref;
};
struct Page {
  long http_status = 0;
  bool access_challenge = false;  // Explicit response header evidence only.
  std::string url, document_url, document_id, title, text;
  std::string referrer_policy;  // Internal policy, excluded from observations.
  std::string document_referrer;
  std::vector<Link> links;
  std::vector<Button> buttons;
  std::vector<Field> fields;
  std::vector<Form> forms;  // Bounded observation metadata; native submission reads the live DOM.
  std::vector<ContentBlock> content;
  // Internal positions preserve DOM order while default observations focus on content.
  std::unordered_map<std::string, std::size_t> anchor_offsets;
  std::size_t observation_offset = 0;
  std::vector<std::string> script_errors;
  std::size_t human_errors_reported = 0;  // Full diagnostics remain in JSON and the log.
  bool truncated = false;
};
}  // namespace webcli
