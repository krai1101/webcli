cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()
project(webcli VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL CONFIG REQUIRED)
find_path(WEBCLI_PSL_INCLUDE_DIR libpsl.h REQUIRED)
find_library(WEBCLI_PSL_LIBRARY_RELEASE NAMES psl libpsl
  PATHS "${WEBCLI_PSL_INCLUDE_DIR}/../lib" REQUIRED NO_DEFAULT_PATH)
find_library(WEBCLI_PSL_LIBRARY_DEBUG NAMES psl libpsl
  PATHS "${WEBCLI_PSL_INCLUDE_DIR}/../debug/lib" NO_DEFAULT_PATH)
include(SelectLibraryConfigurations)
select_library_configurations(WEBCLI_PSL)
find_package(lexbor CONFIG REQUIRED)
if(TARGET lexbor::lexbor)
  set(WEBCLI_LEXBOR_TARGET lexbor::lexbor)
elseif(TARGET lexbor::lexbor_static)
  set(WEBCLI_LEXBOR_TARGET lexbor::lexbor_static)
else()
  message(FATAL_ERROR "Lexbor package exports no supported library target")
endif()
find_package(qjs CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_subdirectory(cmake/wasm)

add_executable(webcli
  src/runtime/wasm/bindings.cpp
  src/runtime/wasm/allocation.cpp
  src/runtime/wasm/objects.cpp
  src/runtime/wasm/calls.cpp
  src/runtime/workers/worker_manager.cpp
  src/runtime/workers/worker_realm.cpp
  src/runtime/workers/worker_bindings.cpp
  src/runtime/frames/frame_tree.cpp
  src/runtime/frames/frame_policy.cpp
  src/runtime/frames/frame_bindings.cpp
  src/runtime/storage/indexed_db_store.cpp
  src/runtime/storage/indexed_db_bindings.cpp
  src/runtime/intl/number_format_bindings.cpp
  src/main.cpp
  src/cli/commands.cpp
  src/cli/instance.cpp
  src/cli/prompt.cpp
  src/cli/terminal.cpp
  src/core/text.cpp
  src/core/runtime_policy.cpp
  src/core/serialization/json_string.cpp
  src/runtime/page/engine_request.cpp
  src/runtime/page/browsing_instance.cpp
  src/runtime/page/lifecycle_delivery.cpp
  src/runtime/navigation/history_bindings.cpp
  src/runtime/navigation/history_archive.cpp
  src/runtime/url/url_bindings.cpp
  src/core/url/url.cpp
  src/core/url/url_components.cpp
  src/runtime/forms/form_submission.cpp
  src/runtime/dom/document_loader.cpp
  src/runtime/dom/tree_usage.cpp
  src/runtime/dom/dom_helpers.cpp
  src/runtime/css/visibility_rules.cpp
  src/host/protocol.cpp
  src/host/resident.cpp
  src/host/instance_worker.cpp
  src/runtime/events/timers.cpp
  src/runtime/engine/diagnostics.cpp
  src/runtime/network/resource_tasks.cpp
  src/runtime/network/completion_delivery.cpp
  src/runtime/navigation/history_clone_bindings.cpp
  src/runtime/intl/segmenter_bindings.cpp
  src/runtime/crypto/digest_bindings.cpp
  src/runtime/crypto/aes_gcm_bindings.cpp
  src/runtime/network/web_bindings.cpp
  src/runtime/dom/node_registry.cpp
  src/runtime/dom/attributes.cpp
  src/runtime/dom/selectors.cpp
  src/runtime/dom/tree_operations.cpp
  src/runtime/dom/node_clone.cpp
  src/runtime/dom/node_adoption.cpp
  src/runtime/messaging/broadcast_channels.cpp
  src/runtime/messaging/message_ports.cpp
  src/runtime/dom/markup.cpp
  src/runtime/dom/controls.cpp
  src/runtime/network/blob_urls.cpp
  src/runtime/scripts/document_executor.cpp
  src/runtime/scripts/parser_coordinator.cpp
  src/runtime/scripts/parser_preloads.cpp
  src/runtime/scripts/document_scripts.cpp
  src/runtime/dom/namespace_bindings.cpp
  src/runtime/dom/component_tree.cpp
  src/runtime/engine/js_runtime.cpp
  src/runtime/engine/native_bindings.cpp
  src/runtime/scripts/module_loader.cpp
  src/runtime/scripts/module_requests.cpp
  src/runtime/scripts/module_contexts.cpp
  src/runtime/scripts/import_map.cpp
  src/runtime/dom/html_parser.cpp
  src/runtime/css/stylesheet_loader.cpp
  src/runtime/css/stylesheet_resources.cpp
  src/runtime/css/stylesheet_bindings.cpp
  src/runtime/css/style_resolver.cpp
  src/network/cookie_store.cpp
  src/network/document_cookies.cpp
  src/network/public_suffix.cpp
  src/network/cors.cpp
  src/network/http_client.cpp
  src/network/fetch_metadata.cpp
  src/network/destination_policy.cpp
  src/network/referrer.cpp
  src/runtime/network/referrer_policy.cpp
  src/network/resource_service.cpp
  src/network/http_handles.cpp
  src/network/data_script.cpp
  src/cli/output/format.cpp
  src/cli/installation.cpp
  src/observation/semantic_extractor.cpp
  src/observation/reference_resolver.cpp
  src/observation/accessible_name.cpp
  src/observation/content_projection.cpp
  src/storage/profile_store.cpp
  src/storage/diagnostic_log.cpp
)
include(cmake/WebApiSources.cmake)
include(cmake/InstallationScripts.cmake)

# Opt-in harness for unchanged upstream tests. Never enabled in release builds.
option(WEBCLI_UPSTREAM_TEST_DRIVER "Build the upstream test driver instead of the CLI" OFF)
if(WEBCLI_UPSTREAM_TEST_DRIVER)
  get_target_property(WEBCLI_TEST_SOURCES webcli SOURCES)
  list(REMOVE_ITEM WEBCLI_TEST_SOURCES src/main.cpp)
  set_property(TARGET webcli PROPERTY SOURCES ${WEBCLI_TEST_SOURCES} tests/upstream/driver.cpp)
  target_compile_definitions(webcli PRIVATE WEBCLI_UPSTREAM_TEST_DRIVER)
  set_target_properties(webcli PROPERTIES OUTPUT_NAME webcli-upstream-driver)
endif()
target_compile_definitions(webcli PRIVATE WEBCLI_VERSION="${PROJECT_VERSION}")
set(WEBCLI_API_SOURCE "")
foreach(API_FILE IN LISTS WEBCLI_WEB_API_SOURCES)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${API_FILE}")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${API_FILE}" API_SOURCE HEX)
  string(REGEX REPLACE "(..)" "0x\\1," API_BYTES "${API_SOURCE}")
  string(APPEND WEBCLI_API_SOURCE "0x0a,${API_BYTES}\n")
endforeach()
set(WEBCLI_WORKER_API_SOURCE "")
foreach(API_FILE IN LISTS WEBCLI_WORKER_API_SOURCES)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${API_FILE}")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${API_FILE}" API_SOURCE HEX)
  string(REGEX REPLACE "(..)" "0x\\1," API_BYTES "${API_SOURCE}")
  string(APPEND WEBCLI_WORKER_API_SOURCE "0x0a,${API_BYTES}\n")
endforeach()
configure_file(src/runtime/embedded_scripts.hpp.in generated/runtime/embedded_scripts.hpp @ONLY)
target_include_directories(webcli PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" "${WEBCLI_PSL_INCLUDE_DIR}")
if(WIN32)
  target_compile_definitions(webcli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(webcli PRIVATE advapi32 shell32 ws2_32)
endif()
target_link_libraries(webcli PRIVATE webcli_wasm unofficial::sqlite3::sqlite3 CURL::libcurl ${WEBCLI_PSL_LIBRARY} ${WEBCLI_LEXBOR_TARGET} qjs Threads::Threads OpenSSL::Crypto ICU::uc ICU::i18n
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)

include(CTest)
if(BUILD_TESTING)
  add_executable(webcli_suffix_tests tests/public_suffix.cpp src/network/public_suffix.cpp)
  target_include_directories(webcli_suffix_tests PRIVATE src "${WEBCLI_PSL_INCLUDE_DIR}")
  target_link_libraries(webcli_suffix_tests PRIVATE CURL::libcurl ${WEBCLI_PSL_LIBRARY})
  add_test(NAME webcli_public_suffix COMMAND webcli_suffix_tests)
  set_tests_properties(webcli_public_suffix PROPERTIES TIMEOUT 10)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME webcli_wasm_contracts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_contracts.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_wasm_contracts PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_runtime_contracts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_contracts.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_runtime_contracts PROPERTIES TIMEOUT 180)
  add_test(NAME webcli_network_security
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/network_security.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_network_security PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_engine_cycle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_cycle.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_engine_cycle PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_diagnostic_rotation
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostic_rotation.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_diagnostic_rotation PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_runtime_diagnostics
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_diagnostics.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_runtime_diagnostics PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_blazedemo_workflow
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/blazedemo_workflow.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_blazedemo_workflow PROPERTIES TIMEOUT 45)
  add_test(NAME webcli_history_workflow
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/history_workflow.py $<TARGET_FILE:webcli>)
  set_tests_properties(webcli_history_workflow PROPERTIES TIMEOUT 45)
endif()

if(MSVC)
  target_compile_options(webcli PRIVATE /W4 /permissive- /utf-8)
  # Keep native headroom above QuickJS's bounded 4 MiB recursion stack.
  target_link_options(webcli PRIVATE /STACK:8388608)
else()
  target_compile_options(webcli PRIVATE -Wall -Wextra -Wpedantic)
  if(MINGW)
    target_link_options(webcli PRIVATE -Wl,--stack,8388608)
  endif()
endif()

if(APPLE)
  set_target_properties(webcli PROPERTIES INSTALL_RPATH "@loader_path")
else()
  set_target_properties(webcli PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS webcli RUNTIME_DEPENDENCY_SET webcli_dependencies RUNTIME DESTINATION .)
install(RUNTIME_DEPENDENCY_SET webcli_dependencies
  PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
  POST_EXCLUDE_REGEXES "[Ss][Yy][Ss][Tt][Ee][Mm]32/" "^/lib/" "^/usr/lib/" "^/System/Library/"
  DIRECTORIES "$<TARGET_FILE_DIR:webcli>" "${WEBCLI_PSL_INCLUDE_DIR}/../bin" "${WEBCLI_PSL_INCLUDE_DIR}/../lib"
  RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES README.md LICENSE NOTICE THIRD_PARTY.md SECURITY.md DESTINATION .)
install(DIRECTORY licenses DESTINATION .)
