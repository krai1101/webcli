#include "runtime/engine/native_bindings.hpp"

#include "runtime/crypto/aes_gcm_bindings.hpp"
#include "runtime/crypto/digest_bindings.hpp"
#include "runtime/css/style_resolver.hpp"
#include "runtime/css/stylesheet_bindings.hpp"
#include "runtime/dom/component_tree.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/html_parser.hpp"
#include "runtime/dom/node_adoption.hpp"
#include "runtime/dom/namespace_bindings.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/native_guard.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/events/timers.hpp"
#include "runtime/frames/frame_tree.hpp"
#include "runtime/workers/worker_manager.hpp"
#include "runtime/messaging/broadcast_channels.hpp"
#include "runtime/intl/number_format_bindings.hpp"
#include "runtime/intl/segmenter_bindings.hpp"
#include "runtime/navigation/history_bindings.hpp"
#include "runtime/navigation/history_clone_bindings.hpp"
#include "runtime/network/blob_urls.hpp"
#include "runtime/network/referrer_policy.hpp"
#include "runtime/network/resource_tasks.hpp"
#include "runtime/network/web_bindings.hpp"
#include "runtime/page/lifecycle_delivery.hpp"
#include "runtime/scripts/parser_coordinator.hpp"
#include "runtime/storage/indexed_db_bindings.hpp"
#include "runtime/url/url_bindings.hpp"

namespace webcli {
void install_native_bindings(JSContext* context, JSValueConst global) {
  install_segmenter_class(context);
  install_number_format_class(context);
  struct Binding {
    const char* name;
    JSCFunction* fn;
    int argc;
  };
  const Binding bindings[] = {
      {"__webcliBroadcast", guarded_binding<js_broadcast_channel>, 3},
      {"__webcliWorker", guarded_binding<js_worker_access>, 4},
      {"__webcliFrameAccess", guarded_binding<js_frame_access>, 4},
      {"__webcliNumberFormat", guarded_binding<js_number_format>, 3},
      {"__webcliIndexedDB", guarded_binding<js_indexed_db>, 2},
      {"__webcliIndexedDBCodec", guarded_binding<js_indexed_db_codec>, 2},
      {"__webcliTaskGeneration", guarded_binding<js_task_generation>, 0},
      {"__webcliSegmenter", guarded_binding<js_segmenter>, 4},
      {"__webcliURLParse", guarded_binding<js_url_parse>, 4},
      {"__webcliDocumentReadiness", guarded_binding<js_document_readiness>, 0},
      {"__webcliHistoryCommit", guarded_binding<js_history_commit>, 4},
      {"__webcliHistoryRead", guarded_binding<js_history_read>, 1},
      {"__webcliHistoryTraverse", guarded_binding<js_history_traverse>, 1},
      {"__webcliComputedVisibility", guarded_binding<js_computed_visibility>, 2},
      {"__webcliPopoverState", guarded_binding<js_popover_state>, 2},
      {"__webcliAdoptedStylesheet", guarded_binding<js_adopted_stylesheet>, 2},
      {"__webcliStyleInvalidate", guarded_binding<js_style_invalidate>, 0},
      {"__webcliIsProxy", guarded_binding<js_is_proxy>, 1},
      {"__webcliCryptoDigest", guarded_binding<js_crypto_digest>, 2},
      {"__webcliCryptoAESGCM", guarded_binding<js_crypto_aes_gcm>, 6},
      {"__webcliStylesheetDisabled", guarded_binding<js_stylesheet_disabled>, 2},
      {"__webcliDOMRevision", guarded_binding<js_dom_revision>, 0},
      {"__webcliDocumentWrite", guarded_binding<js_document_write>, 1},
      {"__webcliRegisterBlobURL", guarded_binding<js_blob_url_register>, 2},
      {"__webcliRevokeBlobURL", guarded_binding<js_blob_url_revoke>, 1},
      {"__webcliReportError", guarded_binding<js_report_error>, 2},
      {"__webcliScriptKind", guarded_binding<js_native_script_kind>, 1},
      {"__webcliLoadImage", guarded_binding<js_native_image_load>, 1},
      {"__webcliLoadScript", guarded_binding<js_native_script_load>, 1},
      {"__webcliExecuteScript", guarded_binding<js_native_script_execute>, 2},
      {"__webcliFetch", guarded_binding<js_native_fetch>, 2},
      {"__webcliReferrerMeta", guarded_binding<js_process_referrer_meta>, 1},
      {"__webcliDocumentReferrer", guarded_binding<js_document_referrer>, 1},
      {"__webcliQueueNavigation", guarded_binding<js_queue_navigation>, 4},
      {"__webcliCancelFetch", guarded_binding<js_native_cancel_fetch>, 1},
      {"__webcliRandomBytes", guarded_binding<js_native_random_bytes>, 1},
      {"__webcliCookie", guarded_binding<js_native_cookie>, 2},
      {"__webcliStorage", guarded_binding<js_native_storage>, 4},
      {"__webcliResolve", guarded_binding<js_native_resolve>, 2},
      {"__webcliNavigate", guarded_binding<js_native_navigate>, 2},
      {"__webcliNodeName", guarded_binding<js_dom_node_name>, 1},
      {"__webcliElementIdentity", guarded_binding<js_dom_element_identity>, 1},
      {"__webcliCreateElementNS", guarded_binding<js_dom_create_element_ns>, 4},
      {"__webcliAttributeNS", guarded_binding<js_dom_attribute_ns>, 5},
      {"__webcliInlineStylesheet", guarded_binding<js_inline_stylesheet>, 2},
      {"__webcliNow", guarded_binding<js_performance_now>, 0},
      {"__webcliNodeType", guarded_binding<js_dom_node_type>, 1},
      {"__webcliParseHTML", guarded_binding<js_parse_html_document>, 1},
      {"__webcliOwnerDocument", guarded_binding<js_node_owner_document>, 1},
      {"__webcliTextGet", guarded_binding<js_dom_text_get>, 1},
      {"__webcliTextSet", guarded_binding<js_dom_text_set>, 2},
      {"__webcliAttrGet", guarded_binding<js_dom_attr_get>, 2},
      {"__webcliAttrSet", guarded_binding<js_dom_attr_set>, 3},
      {"__webcliAttrRemove", guarded_binding<js_dom_attr_remove>, 2},
      {"__webcliQuery", guarded_binding<js_dom_query>, 3},
      {"__webcliCreateElement", guarded_binding<js_dom_create_element>, 1},
      {"__webcliCreateText", guarded_binding<js_dom_create_text>, 1},
      {"__webcliCreateComment", guarded_binding<js_dom_create_comment>, 1},
      {"__webcliAttachShadow", guarded_binding<js_dom_shadow_root>, 1},
      {"__webcliTemplateContent", guarded_binding<js_dom_template_content>, 1},
      {"__webcliCreateFragment", guarded_binding<js_dom_create_fragment>, 0},
      {"__webcliClone", guarded_binding<js_dom_clone>, 3},
      {"__webcliAdopt", guarded_binding<js_dom_adopt>, 2},
      {"__webcliValidateInsert", guarded_binding<js_dom_validate_insert>, 3},
      {"__webcliSelectedIndex", guarded_binding<js_dom_selected_index>, 2},
      {"__webcliTextareaValue", guarded_binding<js_dom_textarea_value>, 2},
      {"__webcliAttributes", guarded_binding<js_dom_attributes>, 1},
      {"__webcliParent", guarded_binding<js_dom_parent>, 1},
      {"__webcliChildren", guarded_binding<js_dom_children>, 1},
      {"__webcliAppend", guarded_binding<js_dom_append>, 2},
      {"__webcliRemove", guarded_binding<js_dom_remove>, 1},
      {"__webcliInsert", guarded_binding<js_dom_insert>, 3},
      {"__webcliHTML", guarded_binding<js_dom_html>, 2},
      {"setTimeout", guarded_binding<js_native_set_timeout>, 2},
      {"queueMicrotask", guarded_binding<js_native_queue_microtask>, 1},
      {"clearTimeout", guarded_binding<js_native_clear_timeout>, 1}};
  for (const auto& binding : bindings) {
    const auto function = JS_NewCFunction(context, binding.fn, binding.name, binding.argc);
    if (JS_IsException(function) || JS_SetPropertyStr(context, global, binding.name, function) < 0)
      throw CliError("RUNTIME_INIT", std::string("cannot install native binding: ") + binding.name);
  }
}
}  // namespace webcli
