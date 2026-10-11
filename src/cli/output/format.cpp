#include "cli/output/format.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "core/errors.hpp"
#include "core/serialization/json_string.hpp"
#include "core/text.hpp"
#include "storage/profile_store.hpp"

namespace webcli {
namespace {
void field_json(std::ostream& out, const Field& f, bool detailed, const Request& request) {
  const auto value =
      f.type == "password" ? "[redacted]" : truncate(f.value, detailed ? 65536 : 512);
  out << "{\"ref\":" << json_escape(f.ref)
      << ",\"role\":" << json_escape(f.type == "select" ? "select" : "input")
      << ",\"name\":" << json_escape(f.label) << ",\"type\":" << json_escape(f.type)
      << ",\"value\":" << json_escape(value) << ",\"disabled\":" << (f.disabled ? "true" : "false")
      << ",\"readonly\":" << (f.readonly ? "true" : "false")
      << ",\"checked\":" << (f.checked ? "true" : "false")
      << ",\"required\":" << (f.required ? "true" : "false");
  if (detailed) {
    out << ",\"fieldName\":" << json_escape(f.name) << ",\"options\":[";
    const auto end = std::min(f.options.size(), request.offset + request.limit);
    for (auto i = std::min(request.offset, end); i < end; ++i)
      out << (i == request.offset ? "" : ",") << json_escape(truncate(f.options[i], 256));
    out << "],\"optionCount\":" << f.options.size();
    out << ",\"nextOffset\":" << (end < f.options.size() ? std::to_string(end) : "null");
  }
  if (!detailed && f.type == "select") {
    out << ",\"options\":[";
    const auto count = std::min<std::size_t>(f.options.size(), 20);
    for (std::size_t i = 0; i < count; ++i)
      out << (i ? "," : "") << json_escape(truncate(f.options[i], 80));
    out << "],\"optionCount\":" << f.options.size()
        << ",\"optionsTruncated\":" << (count < f.options.size() ? "true" : "false");
  }
  out << '}';
}
void link_json(std::ostream& out, const Link& l) {
  out << "{\"ref\":" << json_escape(l.ref) << ",\"role\":\"link\",\"name\":" << json_escape(l.label)
      << ",\"url\":" << json_escape(l.url) << '}';
}
void button_json(std::ostream& out, const Button& b) {
  out << "{\"ref\":" << json_escape(b.ref) << ",\"role\":" << json_escape(b.role)
      << ",\"name\":" << json_escape(b.label) << ",\"type\":" << json_escape(b.type);
  if (b.role == "tab") out << ",\"selected\":" << (b.selected ? "true" : "false");
  out << '}';
}
}  // namespace

void render(const Page& page, const Request& request, std::ostream& out) {
  const auto begin =
      std::min(request.offset_set ? request.offset : page.observation_offset, page.content.size());
  const auto end = std::min(page.content.size(), begin + request.limit);
  std::set<std::string> refs;
  std::string text;
  for (auto i = begin; i < end; ++i) {
    const auto& block = page.content[i];
    if (!text.empty()) text += '\n';
    text += block.text;
    if (!block.ref.empty()) refs.insert(block.ref);
  }
  if (!request.json) {
    out << "URL: " << page.url << "\nTITLE: " << page.title << "\n\n";
    if (request.debug)
      out << "Diagnostics: document " << page.document_id << " | HTTP " << page.http_status
          << " | retained JavaScript warnings " << page.script_errors.size() << "\n\n";
    for (auto i = begin; i < end; ++i) {
      const auto& b = page.content[i];
      if (!b.ref.empty())
        out << '[' << b.ref << "] ";
      else if (b.kind == "heading")
        out << "# ";
      else if (b.kind == "list-item")
        out << "- ";
      else if (b.kind == "table-row")
        out << "| ";
      out << b.text << '\n';
    }
    if (begin == end) out << "(no content at this offset)\n";
    if (page.access_challenge)
      out << "Access challenge: confirmed by the response header; page access is incomplete.\n";
    else if (page.content.empty())
      out << "Empty document: no observable content; this does not establish website "
             "compatibility.\n";
    if (end < page.content.size())
      out << "\nMore: " << "observe" << " --offset " << end << " --limit " << request.limit << '\n';
    if (page.truncated) out << "Extraction reached the document safety limit.\n";
    if (!page.script_errors.empty()) {
      out << "\nJavaScript warnings: " << page.script_errors.size()
          << " (page behavior may be incomplete).\n";
      const auto warning_begin = request.debug ? 0 : page.human_errors_reported;
      const auto warning_end = request.debug
                                   ? page.script_errors.size()
                                   : std::min(page.script_errors.size(), warning_begin + 3);
      for (std::size_t i = warning_begin; i < warning_end; ++i) {
        const auto& error = page.script_errors[i];
        if (request.debug) {
          out << "  [" << i + 1 << "] ";
          for (std::size_t start = 0; start < error.size();) {
            const auto newline = error.find('\n', start);
            if (newline == std::string::npos) {
              out << error.substr(start);
              break;
            }
            out << error.substr(start, newline - start) << "\n      ";
            start = newline + 1;
          }
          out << '\n';
          continue;
        }
        auto summary = error.substr(0, error.find('\n'));
        // Some error reporters put the actual message after a heading and a
        // blank line. Include that message instead of repeating only headings.
        if (!summary.empty() && summary.back() == ':') {
          const auto newline = error.find('\n');
          if (newline != std::string::npos) {
            const auto message = error.find_first_not_of("\r\n\t ", newline);
            if (message != std::string::npos)
              summary += " " + error.substr(message, error.find('\n', message) - message);
          }
        }
        // Inline script payloads are source identifiers, not useful prompt text.
        // Preserve the complete source and error in JSON and diagnostics.
        if (summary.rfind("data:", 0) == 0) {
          const auto message = summary.find(": ");
          if (message != std::string::npos)
            summary = "inline data script" + summary.substr(message);
        }
        out << "  " << summary << '\n';
      }
      if (warning_end < page.script_errors.size())
        out << "  " << page.script_errors.size() - warning_end
            << " additional warnings in diagnostics.\n";
      out << "Full diagnostics: " << (diagnostic_directory() / "runtime.jsonl").string()
          << " (also in --json).\n";
    }
    return;
  }
  out << "{\"url\":" << json_escape(page.url) << ",\"title\":" << json_escape(page.title)
      << ",\"documentId\":" << json_escape(page.document_id) << ",\"text\":" << json_escape(text);
  const auto outcome = page.access_challenge                           ? "access-challenge"
                       : page.content.empty()                          ? "empty"
                       : !page.script_errors.empty() || page.truncated ? "partial"
                                                                       : "content";
  out << ",\"outcome\":{\"status\":" << json_escape(outcome)
      << ",\"httpStatus\":" << page.http_status
      << ",\"runtimeWarnings\":" << page.script_errors.size()
      << ",\"extractionTruncated\":" << (page.truncated ? "true" : "false") << ",\"evidence\":"
      << (page.access_challenge ? "\"response header confirms access challenge\"" : "null") << '}';
  {
    out << ",\"content\":[";
    for (auto i = begin; i < end; ++i) {
      const auto& b = page.content[i];
      out << (i == begin ? "" : ",") << "{\"kind\":" << json_escape(b.kind)
          << ",\"text\":" << json_escape(b.text);
      if (!b.ref.empty()) out << ",\"ref\":" << json_escape(b.ref);
      out << '}';
    }
    out << "],\"links\":[";
    bool first = true;
    for (const auto& l : page.links)
      if (refs.count(l.ref)) {
        if (!first) out << ',';
        link_json(out, l);
        first = false;
      }
    out << "],\"buttons\":[";
    first = true;
    for (const auto& b : page.buttons)
      if (refs.count(b.ref)) {
        if (!first) out << ',';
        button_json(out, b);
        first = false;
      }
    out << "],\"fields\":[";
    first = true;
    for (const auto& f : page.fields)
      if (refs.count(f.ref)) {
        if (!first) out << ',';
        field_json(out, f, false, request);
        first = false;
      }
    out << ']';
  }
  out << ",\"pagination\":{\"offset\":" << begin << ",\"limit\":" << request.limit
      << ",\"total\":" << page.content.size()
      << ",\"nextOffset\":" << (end < page.content.size() ? std::to_string(end) : "null")
      << ",\"truncated\":" << (page.truncated ? "true" : "false") << "},\"scriptErrors\":[";
  for (std::size_t i = 0; i < page.script_errors.size(); ++i)
    out << (i ? "," : "") << json_escape(page.script_errors[i]);
  out << ']';
  if (request.debug)
    out << ",\"diagnostics\":{\"enabled\":true,\"warningCount\":" << page.script_errors.size()
        << ",\"logPath\":" << json_escape((diagnostic_directory() / "runtime.jsonl").string())
        << '}';
  out << "}\n";
}
void render_error(std::ostream& out, bool json, const std::string& code, const std::string& message,
                  bool retryable) {
  if (json) {
    const auto status = code == "SUBMISSION_OUTCOME_UNKNOWN" ? "uncertain-submission"
                        : code.rfind("NETWORK", 0) == 0      ? "transport-failure"
                        : code == "HTTP_STATUS"              ? "http-error"
                                                             : "command-error";
    out << "{\"error\":" << json_escape(code) << ",\"message\":" << json_escape(message)
        << ",\"retryable\":" << (retryable ? "true" : "false")
        << ",\"outcome\":{\"status\":" << json_escape(status) << "}}\n";
  } else
    out << "ERROR [" << code << "]: " << message << '\n';
}
}  // namespace webcli
