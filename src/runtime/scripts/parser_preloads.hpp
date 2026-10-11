#pragma once
#include <string>
namespace webcli {
class JsRuntime;
struct ScriptContext;
std::string await_parser_source(JsRuntime&, ScriptContext&, int node_id);
}  // namespace webcli
