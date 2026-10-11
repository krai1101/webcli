#pragma once

#include "core/page.hpp"

struct lxb_dom_node;
namespace webcli {
class JsRuntime;
std::string normalize_form_method(std::string);
std::string normalize_form_encoding(std::string);
// Enumerate live successful-control candidates independently of observation
// limits, composed rendering order, and hidden-element filtering.
Form collect_submission_form(lxb_dom_node*, JsRuntime&, const std::string& document_url);
struct FormNavigation {
  std::string url, method, body;
};
FormNavigation prepare_form_navigation(JsRuntime&, int form, int button,
                                      const std::string& document_url);
}  // namespace webcli
