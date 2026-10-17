#include "observation/semantic_extractor.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

#include "core/text.hpp"
#include "network/http_client.hpp"
#include "observation/accessible_name.hpp"
#include "observation/content_projection.hpp"
#include "observation/link_groups.hpp"
#include "runtime/css/visibility_rules.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"

namespace webcli {
std::string SemanticExtractor::ref(int frame, std::uint64_t generation, int id, char role) {
  const auto key = std::make_tuple(frame, generation, id, role);
  auto it = refs_.find(key);
  if (it != refs_.end()) return it->second;
  return refs_.emplace(key, std::string(1, role) + std::to_string(++counts_[role])).first->second;
}
void SemanticExtractor::extract(lxb_html_document_t* document, Page& page, JsRuntime& runtime) {
  auto& state = runtime.script_context();
  const auto generation = state.frame_tree->generation(state.frame_id);
  if (!state.frame_id) {
    for (auto entry = refs_.begin(); entry != refs_.end();) {
      const auto frame = std::get<0>(entry->first);
      if (frame && state.frame_tree->generation(frame) != std::get<1>(entry->first))
        entry = refs_.erase(entry);
      else
        ++entry;
    }
  }
  auto reference = [&](int node, char role) { return ref(state.frame_id, generation, node, role); };
  runtime.prepare_composition();
  const auto base_url = runtime.document_base_url();
  page.links.clear();
  page.buttons.clear();
  page.fields.clear();
  page.forms.clear();
  page.content.clear();
  page.text.clear();
  page.truncated = false;
  auto* root = lxb_dom_interface_node(&document->dom_document);
  std::vector<lxb_dom_node_t*> nodes;
  std::function<void(lxb_dom_node_t*, int)> collect = [&](lxb_dom_node_t* node, int depth) {
    if (!node || depth > 256 || nodes.size() >= 50000) {
      page.truncated = true;
      return;
    }
    nodes.push_back(node);
    for (auto* child : runtime.composed_children(node)) collect(child, depth + 1);
  };
  collect(root, 0);
  // Stylesheets cannot cross a shadow boundary. Slotted nodes retain their
  // light-tree scope even though observation follows their composed position.
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> scopes;
  std::unordered_map<lxb_dom_node_t*, std::vector<lxb_dom_node_t*>> scope_nodes;
  for (auto* node : nodes) {
    auto* scope = node;
    while (scope->parent) scope = scope->parent;
    scopes[node] = scope;
    scope_nodes[scope].push_back(node);
  }
  const auto& visibility = runtime.visibility_values(page.truncated);
  page.title = "(untitled)";
  bool title_seen = false;
  std::map<std::pair<lxb_dom_node_t*, std::string>, std::string> labels;
  std::map<std::pair<lxb_dom_node_t*, std::string>, lxb_dom_node_t*> label_nodes;
  std::set<lxb_dom_node_t*> consumed_labels;
  LabelReferences label_references;
  std::map<lxb_dom_node_t*, int> form_owners;
  std::map<std::pair<lxb_dom_node_t*, std::string>, int> form_ids;
  for (auto* node : nodes) {
    const auto tag = tag_name(node);
    const auto element_id = attr(node, "id");
    if (!element_id.empty())
      label_references.emplace(std::make_pair(scopes.at(node), element_id), node);
    if (!title_seen && tag == "title" && node->ns == LXB_NS_HTML && scopes.at(node) == root) {
      page.title = truncate(trim(runtime.composed_text(node)), 300);
      title_seen = true;
    }
    if (tag == "label" && !attr(node, "for").empty()) {
      labels[{scopes.at(node), attr(node, "for")}] = trim(runtime.composed_text(node));
      label_nodes[{scopes.at(node), attr(node, "for")}] = node;
    }
    if (tag == "form" && page.forms.size() < kMaxForms) {
      Form form;
      form.node_id = runtime.node_id(node);
      form.frame_id = state.frame_id;
      form.selector = css_path(node);
      auto action =
          resolve_web_url(base_url, attr(node, "action").empty() ? base_url : attr(node, "action"));
      form.action = action.value_or("");
      form.method = attr(node, "method");
      std::transform(form.method.begin(), form.method.end(), form.method.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (form.method.empty()) form.method = "get";
      const auto index = static_cast<int>(page.forms.size());
      form_owners[node] = index;
      if (!attr(node, "id").empty()) form_ids[{scopes.at(node), attr(node, "id")}] = index;
      page.forms.push_back(std::move(form));
    } else if (tag == "form")
      page.truncated = true;
  }
  std::unordered_map<lxb_dom_node_t*, bool> ignored_cache;
  const std::function<lxb_dom_node_t*(lxb_dom_node_t*)> composed_parent =
      [&](lxb_dom_node_t* node) { return runtime.composed_parent(node); };
  auto ignored = [&](lxb_dom_node_t* node) {
    // Projection can encounter descendants beyond the bounded metadata scan.
    if (!scopes.count(node)) return true;
    auto found = ignored_cache.find(node);
    if (found != ignored_cache.end()) return found->second;
    // Agent observations follow accessibility visibility for both text and
    // controls. Modal backdrops can remain painted while aria-hidden excludes
    // their entire subtree from the current interaction context.
    const auto value = ignored_subtree(node, false, true) ||
                       stylesheet_display_none(node, visibility) ||
                       (node->type == LXB_DOM_NODE_TYPE_TEXT &&
                        stylesheet_invisible(node, visibility, composed_parent));
    ignored_cache.emplace(node, value);
    return value;
  };
  lxb_dom_node_t* modal = nullptr;
  bool restrict_to_modal = false;
  auto hidden = [&](lxb_dom_node_t* node) {
    if (restrict_to_modal) {
      bool inside = false;
      for (auto* ancestor = node; ancestor; ancestor = runtime.composed_parent(ancestor))
        if (ancestor == modal) { inside = true; break; }
      if (!inside) return true;
    }
    for (auto* parent = node; parent; parent = runtime.composed_parent(parent))
      if (ignored(parent) || ignored_subtree(parent, false, true)) return true;
    return stylesheet_invisible(node, visibility, composed_parent);
  };
  // aria-modal identifies custom dialogs; native modal state is tracked
  // separately because an ordinary dialog.show() does not block its page.
  const auto concealed_background = [&](lxb_dom_node_t* dialog) {
    // Some dialog libraries signal modality by excluding surrounding content
    // from accessibility, rather than setting aria-modal on the dialog itself.
    for (auto* branch = dialog; branch; branch = runtime.composed_parent(branch)) {
      auto* parent = runtime.composed_parent(branch);
      if (!parent) break;
      for (auto* sibling : runtime.composed_children(parent)) {
        const auto tag = tag_name(sibling);
        if (sibling == branch || tag.empty() || tag == "svg" || tag == "script" || tag == "style")
          continue;
        if (has_attr(sibling, "inert") || attr(sibling, "aria-hidden") == "true") return true;
      }
    }
    return false;
  };
  std::uint64_t native_order = 0;
  for (auto* node : nodes) {
    if (hidden(node)) continue;
    const auto native = state.dom.modal_dialogs.find(node);
    if (has_attr(node, "open") && native != state.dom.modal_dialogs.end()) {
      if (native->second > native_order) {
        native_order = native->second;
        modal = node;
      }
    } else if (!native_order &&
               (attr(node, "role") == "dialog" || attr(node, "role") == "alertdialog") &&
               (attr(node, "aria-modal") == "true" ||
                (attr(node, "aria-modal") != "false" && concealed_background(node)))) {
      modal = node;
    }
  }
  if (modal) {
    restrict_to_modal = true;
    Button outside;
    outside.label = "Outside dialog";
    outside.type = "button";
    outside.node_id = runtime.node_id(modal);
    outside.frame_id = state.frame_id;
    outside.ref = reference(outside.node_id, 'B');
    outside.outside_modal = true;
    page.content.push_back({"heading", "Modal", ""});
    page.content.push_back({"button", outside.label, outside.ref});
    page.buttons.push_back(std::move(outside));
  }
  std::map<lxb_dom_node_t*, std::string> visible_refs;
  std::map<lxb_dom_node_t*, std::string> visible_labels;
  std::set<lxb_dom_node_t*> suppressed_links;
  LinkGroups link_groups;
  for (auto* node : nodes) {
    const auto tag = tag_name(node);
    const auto role = attr(node, "role");
    const bool menu_action =
        role == "menuitem" || role == "menuitemcheckbox" || role == "menuitemradio";
    // ARIA links can be script-driven elements without an HTML href. They
    // still need an action ref; clicking dispatches through their own realm.
    const bool scripted_link = role == "link" && !(tag == "a" && has_attr(node, "href"));
    const bool editable = node->type == LXB_DOM_NODE_TYPE_ELEMENT && is_content_editable(node) &&
                          !is_content_editable(node->parent);
    if (tag != "a" && tag != "button" && tag != "input" && tag != "select" && tag != "textarea" &&
        role != "button" && role != "tab" && !menu_action && !scripted_link && !editable)
      continue;
    const auto id = runtime.node_id(node);
    if (id < 0) {
      page.truncated = true;
      break;
    }
    const bool invisible = hidden(node);
    auto label = invisible ? attr(node, "aria-label")
                           : referenced_name(node, scopes.at(node), label_references, runtime,
                                             consumed_labels);
    lxb_dom_node_t* associated_label = nullptr;
    if (labels.count({scopes.at(node), attr(node, "id")})) {
      associated_label = label_nodes.at({scopes.at(node), attr(node, "id")});
      if (label.empty()) label = labels.at({scopes.at(node), attr(node, "id")});
    }
    if (!associated_label)
      for (auto* p = lxb_dom_node_parent(node); p; p = lxb_dom_node_parent(p))
        if (tag_name(p) == "label") {
          associated_label = p;
          if (label.empty()) label = trim(runtime.composed_text(p));
          break;
        }
    // Some real forms use a broken label 'for'. Infer only within a small group
    // containing exactly one label and one control, never across a whole form.
    if (!associated_label && (tag == "input" || tag == "textarea" || tag == "select")) {
      auto* group = lxb_dom_node_parent(node);
      for (int level = 0; group && level < 3; ++level, group = lxb_dom_node_parent(group)) {
        const auto group_tag = tag_name(group);
        if (group_tag == "form" || group_tag == "body" || group_tag == "html") break;
        int controls = 0, found_labels = 0;
        lxb_dom_node_t* candidate = nullptr;
        std::vector<lxb_dom_node_t*> queue{group};
        for (std::size_t index = 0; index < queue.size() && queue.size() <= 100; ++index) {
          const auto child_tag = tag_name(queue[index]);
          if (child_tag == "input" || child_tag == "textarea" || child_tag == "select" ||
              child_tag == "button")
            ++controls;
          if (child_tag == "label") {
            ++found_labels;
            candidate = queue[index];
          }
          for (auto* child = queue[index]->first_child; child; child = child->next)
            queue.push_back(child);
        }
        if (queue.size() <= 100 && controls == 1 && found_labels == 1) {
          associated_label = candidate;
          if (label.empty()) label = trim(runtime.composed_text(candidate));
          break;
        }
        if (controls > 1) break;
      }
    }
    if (label.empty() && tag != "select") label = trim(runtime.composed_text(node));
    if (label.empty()) label = attr(node, "title");
    if (label.empty() && (tag == "a" || tag == "button")) {
      // Image-only actions commonly expose their label through the image alt.
      // Bound the search and exclude images hidden from the observation.
      std::vector<lxb_dom_node_t*> queue{node};
      for (std::size_t index = 0; index < queue.size() && index < 100; ++index) {
        auto* child = queue[index];
        if (hidden(child)) continue;
        if (tag_name(child) == "img") {
          const auto alternative = trim(attr(child, "alt"));
          if (!alternative.empty()) {
            if (!label.empty()) label += " ";
            label += truncate(alternative, 180);
            if (label.size() >= 180) break;
          }
        }
        for (auto* descendant = child->first_child; descendant && queue.size() < 100;
             descendant = descendant->next)
          queue.push_back(descendant);
      }
    }
    if (associated_label && !invisible) consumed_labels.insert(associated_label);
    const auto type_attr = control_type(node);
    const bool button = tag == "button" ||
                        (tag == "input" && (type_attr == "submit" || type_attr == "button" ||
                                            type_attr == "reset")) ||
                        role == "button" || role == "tab" || menu_action || scripted_link;
    if (!invisible &&
        ((tag == "a" && !button && has_attr(node, "href") && page.links.size() >= kMaxLinks) ||
         (button && page.buttons.size() >= kMaxButtons)))
      page.truncated = true;
    if (!invisible && tag == "a" && !button && has_attr(node, "href") &&
        page.links.size() < kMaxLinks) {
      auto url = resolve_web_url(base_url, attr(node, "href"));
      if (url) {
        if (label.empty()) label = *url;
        if (link_groups.repeated(node, *url, label)) {
          suppressed_links.insert(node);
          continue;
        }
        auto r = reference(id, 'L');
        page.links.push_back({truncate(label, 180), *url, css_path(node), r, id});
        page.links.back().frame_id = state.frame_id;
        visible_refs[node] = r;
        visible_labels[node] = truncate(label, 180);
      }
    } else if (!invisible && button && page.buttons.size() < kMaxButtons) {
      if (label.empty()) label = attr(node, "value");
      auto type = type_attr.empty() ? (tag == "button" ? "submit" : "button") : type_attr;
      if (label.empty()) label = type == "submit" ? "Submit" : type == "reset" ? "Reset" : "Button";
      auto r = reference(id, 'B');
      page.buttons.push_back({truncate(label, 180), type, css_path(node), r, id});
      page.buttons.back().frame_id = state.frame_id;
      if (scripted_link) page.buttons.back().role = "link";
      visible_refs[node] = r;
      visible_labels[node] = truncate(label, 180);
      if (role == "tab") {
        page.buttons.back().role = "tab";
        page.buttons.back().selected = attr(node, "aria-selected") == "true";
        visible_labels[node] +=
            " [tab" + std::string(page.buttons.back().selected ? ", selected" : "") + "]";
      }
    }
    if (tag != "input" && tag != "textarea" && tag != "select" && tag != "button" && !editable)
      continue;
    Field field;
    field.node_id = id;
    field.frame_id = state.frame_id;
    field.id = attr(node, "id");
    field.name = attr(node, "name");
    field.selector = css_path(node);
    field.type =
        editable && tag != "input" && tag != "textarea" && tag != "select" && tag != "button"
            ? "contenteditable"
        : tag == "select"   ? "select"
        : tag == "textarea" ? "textarea"
        : type_attr.empty() ? (tag == "button" ? "submit" : "text")
                            : type_attr;
    field.label = label;
    if (field.label.empty()) field.label = attr(node, "placeholder");
    if (field.label.empty()) field.label = field.name.empty() ? field.id : field.name;
    field.value = attr(node, "value");
    field.disabled = is_disabled_control(node);
    field.readonly = has_attr(node, "readonly") &&
                     (tag == "textarea" ||
                      (tag == "input" && field.type != "checkbox" && field.type != "radio" &&
                       field.type != "image" && field.type != "file" && field.type != "range" &&
                       field.type != "color" && field.type != "hidden" && field.type != "button" &&
                       field.type != "submit" && field.type != "reset"));
    field.checked = has_attr(node, "checked");
    field.required = has_attr(node, "required");
    if (tag == "textarea") field.value = runtime.textarea_value(node);
    if (field.type == "contenteditable") field.value = runtime.composed_text(node);
    if ((field.type == "checkbox" || field.type == "radio") && !has_attr(node, "value"))
      field.value = "on";
    if (tag == "select") {
      std::function<void(lxb_dom_node_t*, int)> options = [&](lxb_dom_node_t* n, int depth) {
        if (!n || depth > 256 || field.options.size() >= 1000) return;
        if (tag_name(n) == "option") {
          auto value = has_attr(n, "value") ? attr(n, "value") : trim(runtime.composed_text(n));
          field.options.push_back(value);
        }
        for (auto* child = lxb_dom_node_first_child(n); child; child = lxb_dom_node_next(child))
          options(child, depth + 1);
      };
      options(node, 0);
      const auto selected = runtime.select_value(node);
      field.has_selected_value = selected.first;
      field.value = selected.second;
    }
    if (has_attr(node, "form")) {
      const auto owner = form_ids.find({scopes.at(node), attr(node, "form")});
      if (owner != form_ids.end()) field.form_index = owner->second;
    } else
      for (auto* p = lxb_dom_node_parent(node); p; p = lxb_dom_node_parent(p))
        if (form_owners.count(p)) {
          field.form_index = form_owners.at(p);
          break;
        }
    if (field.form_index >= 0)
      page.forms[static_cast<std::size_t>(field.form_index)].fields.push_back(field);
    if (!invisible && !button && field.type != "hidden" && field.type != "reset" &&
        page.fields.size() >= kMaxFormFields)
      page.truncated = true;
    if (!invisible && !button && field.type != "hidden" && field.type != "reset" &&
        page.fields.size() < kMaxFormFields) {
      field.ref = reference(id, tag == "select" ? 'S' : 'I');
      visible_refs[node] = field.ref;
      auto description = field.label + " (" + field.type + ")";
      if (field.type == "checkbox" || field.type == "radio")
        description += field.checked ? " [checked]" : " [unchecked]";
      else
        description +=
            " = " + (field.type == "password" ? "[redacted]" : truncate(field.value, 512));
      if (field.disabled) description += " [disabled]";
      if (field.readonly) description += " [readonly]";
      if (field.required) description += " [required]";
      if (field.type == "select") {
        description += " | options: ";
        for (std::size_t i = 0; i < std::min<std::size_t>(field.options.size(), 10); ++i)
          description += (i ? ", " : "") + truncate(field.options[i], 80);
        if (field.options.size() > 10) description += ", ...";
      }
      visible_labels[node] = std::move(description);
      page.fields.push_back(std::move(field));
    }
  }
  auto embedded_content = [&](int id, const std::string& title) {
    auto* child = state.frame_tree->runtime(id);
    if (!child) return std::vector<ContentBlock>{};
    Page embedded;
    embedded.url = child->script_context().history_source_url;
    child->sync(embedded);
    extract(child->script_context().dom.document, embedded, *child);
    page.truncated = page.truncated || embedded.truncated;
    auto append = [&](auto& destination, auto& source, std::size_t maximum) {
      if (source.size() > maximum - std::min(maximum, destination.size())) page.truncated = true;
      const auto count = std::min(source.size(), maximum - std::min(maximum, destination.size()));
      destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                         std::make_move_iterator(source.begin() + count));
    };
    append(page.links, embedded.links, kMaxLinks);
    append(page.buttons, embedded.buttons, kMaxButtons);
    append(page.fields, embedded.fields, kMaxFormFields);
    append(page.forms, embedded.forms, kMaxForms);
    // A bounded observation must not advertise actions whose metadata
    // could not be retained. Text remains readable at the shared limit.
    std::set<std::string> retained;
    for (const auto& link : page.links) retained.insert(link.ref);
    for (const auto& button : page.buttons) retained.insert(button.ref);
    for (const auto& field : page.fields) retained.insert(field.ref);
    embedded.content.erase(std::remove_if(embedded.content.begin(), embedded.content.end(),
                                          [&](const auto& block) {
                                            return !block.ref.empty() && !retained.count(block.ref);
                                          }),
                           embedded.content.end());
    if (!title.empty() && !embedded.content.empty())
      embedded.content.insert(embedded.content.begin(), {"heading", truncate(title, 180), ""});
    return std::move(embedded.content);
  };
  project_content(modal ? modal : root, page,
                  {runtime, visible_refs, visible_labels, consumed_labels, suppressed_links, nodes,
                   ignored, [&](lxb_dom_node_t* node) {
                     if (hidden(node)) return std::vector<ContentBlock>{};
                     return embedded_content(state.frame_tree->ensure(state, node),
                                             trim(attr(node, "title")));
                   }});
  // Auxiliary windows are separate browsing contexts, not hidden DOM frames.
  // Expose their controls with normal refs so agents can operate sign-in flows.
  if (!state.frame_id)
    for (int id : state.frame_tree->popups()) {
      auto* child = state.frame_tree->runtime(id);
      if (!child) continue;
      // A newly opened about:blank context is still a window, even before it
      // acquires observable content. Do not make popup creation disappear.
      auto content = embedded_content(id, "");
      page.content.push_back(
          {"heading", truncate("Window: " + child->script_context().history_source_url, 180), ""});
      if (content.empty())
        page.content.push_back({"text", "Window has no observable content yet.", ""});
      page.content.insert(page.content.end(), std::make_move_iterator(content.begin()),
                          std::make_move_iterator(content.end()));
    }
}
}  // namespace webcli
