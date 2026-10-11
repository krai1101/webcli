#pragma once
#include <lexbor/html/html.h>
#include <quickjs.h>

#include <cstdint>
#include <memory>
#include <string>

namespace webcli {
struct ScriptContext;
class JsRuntime;
struct FrameEnvironment {
  ScriptContext* parent = nullptr;
  class FrameTree* tree = nullptr;
  int id = 0;
  std::shared_ptr<lxb_html_document_t> document;
  std::string origin, base;
  bool opaque = false;
  bool scripts = true;
};
// One frame tree per top-level document. Only the engine thread touches realms;
// background workers return response data and never hold JavaScript values.
class FrameTree {
 public:
  explicit FrameTree(ScriptContext&);
  ~FrameTree();
  FrameTree(const FrameTree&) = delete;
  FrameTree& operator=(const FrameTree&) = delete;
  int ensure(ScriptContext&, lxb_dom_node_t*);
  void bind(ScriptContext&);
  void changed(ScriptContext&, lxb_dom_node_t*);
  void scan(ScriptContext&);
  void pump();
  void cancel_workers();
  bool pending() const;
  bool matches(const std::string& selector) const;
  ScriptContext* state(int) const;
  JsRuntime* runtime(int) const;
  std::uint64_t generation(int) const;
  std::uint64_t observation_revision() const;
  int parent(int) const;
  lxb_dom_node_t* element(int) const;
  int child(int, int) const;
  int count(int) const;
  void navigate(int, std::string, std::string method = "GET", std::string body = "",
                bool submission = false);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
void retain_frame_realm(ScriptContext&, std::shared_ptr<ScriptContext>);
JSValue js_frame_access(JSContext*, JSValueConst, int, JSValueConst*);
JSValue frame_security_error(JSContext*, const char*);
}  // namespace webcli
