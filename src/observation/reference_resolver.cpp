#include "observation/reference_resolver.hpp"

namespace webcli {
ReferenceHints describe_references(const Page& page) {
  ReferenceHints hints;
  hints.reserve(page.links.size() + page.buttons.size() + page.fields.size());
  for (const auto& link : page.links) {
    ReferenceHint hint;
    hint.kind = 'L';
    hint.frame = link.frame_id;
    hint.label = link.label;
    hint.url = link.url;
    hints.emplace(link.ref, std::move(hint));
  }
  for (const auto& button : page.buttons) {
    // A backdrop action belongs to this exact modal, not any later dialog
    // that happens to expose the same generated label.
    if (button.outside_modal) continue;
    ReferenceHint hint;
    hint.kind = 'B';
    hint.frame = button.frame_id;
    hint.label = button.label;
    hint.type = button.type;
    hint.role = button.role;
    hints.emplace(button.ref, std::move(hint));
  }
  for (const auto& field : page.fields) {
    ReferenceHint hint;
    hint.kind = field.ref.front();
    hint.frame = field.frame_id;
    hint.label = field.label;
    hint.type = field.type;
    hint.id = field.id;
    hint.name = field.name;
    hints.emplace(field.ref, std::move(hint));
  }
  return hints;
}
std::optional<std::string> recover_reference(const Page& page, const ReferenceHint& hint) {
  // Unnamed controls have no useful semantic identity for recovery.
  if (hint.label.empty()) return std::nullopt;
  std::optional<std::string> match;
  unsigned count = 0;
  const auto accept = [&](const std::string& ref) {
    match = ref;
    ++count;
  };
  if (hint.kind == 'L') {
    for (const auto& link : page.links)
      if (link.frame_id == hint.frame && link.label == hint.label && link.url == hint.url)
        accept(link.ref);
  } else if (hint.kind == 'B') {
    for (const auto& button : page.buttons)
      if (!button.outside_modal && button.frame_id == hint.frame && button.label == hint.label &&
          button.type == hint.type && button.role == hint.role)
        accept(button.ref);
  } else {
    for (const auto& field : page.fields)
      if (field.frame_id == hint.frame && field.ref.front() == hint.kind &&
          field.label == hint.label && field.type == hint.type && field.id == hint.id &&
          field.name == hint.name)
        accept(field.ref);
    if (count == 0) {
      // Hydration can generate new HTML ids/names for the same accessible
      // control. Like agent-browser's role/name fallback, recover its semantic
      // identity only when unique. Preserve the input type so a password field
      // with the same accessible name cannot receive a text-field action.
      for (const auto& field : page.fields)
        if (field.frame_id == hint.frame && field.ref.front() == hint.kind &&
            field.label == hint.label && field.type == hint.type)
          accept(field.ref);
    }
  }
  return count == 1 ? match : std::nullopt;
}
}  // namespace webcli
