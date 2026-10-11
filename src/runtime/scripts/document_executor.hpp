#pragma once
#include <vector>
namespace webcli {
class JsRuntime;
struct ScriptContext;
struct Page;
struct DocumentScript;
void execute_document_scripts(JsRuntime&, ScriptContext&, Page&, std::vector<DocumentScript>&,
                              bool finish);
}  // namespace webcli
