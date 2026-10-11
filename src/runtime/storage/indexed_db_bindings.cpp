#include "runtime/storage/indexed_db_bindings.hpp"

#include <unordered_map>
#include <vector>

#include "network/http_client.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"

namespace webcli {
namespace {
// Only predefined statements are exposed to JS. Pages cannot supply SQL or
// filesystem paths, and all page-controlled names and values are parameters.
const std::unordered_map<std::string, const char*> statements = {
    {"begin", "BEGIN IMMEDIATE"},
    {"commit", "COMMIT"},
    {"rollback", "ROLLBACK"},
    {"save", "SAVEPOINT record_operation"},
    {"release", "RELEASE record_operation"},
    {"undo", "ROLLBACK TO record_operation"},
    {"databases", "SELECT name,version FROM databases ORDER BY name"},
    {"database", "SELECT version FROM databases WHERE name=?"},
    {"version",
     "INSERT INTO databases(name,version) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET "
     "version=excluded.version"},
    {"deleteDatabase", "DELETE FROM databases WHERE name=?"},
    {"stores",
     "SELECT name,key_path,automatic,next_key FROM stores WHERE database_name=? ORDER BY name"},
    {"createStore", "INSERT INTO stores(database_name,name,key_path,automatic) VALUES(?,?,?,?)"},
    {"deleteStore", "DELETE FROM stores WHERE database_name=? AND name=?"},
    {"generator", "SELECT next_key FROM stores WHERE database_name=? AND name=?"},
    {"advance", "UPDATE stores SET next_key=max(next_key,?) WHERE database_name=? AND name=?"},
    {"add", "INSERT INTO records(database_name,store_name,key,value) VALUES(?,?,?,?)"},
    {"put",
     "INSERT INTO records(database_name,store_name,key,value) VALUES(?,?,?,?) ON "
     "CONFLICT(database_name,store_name,key) DO UPDATE SET value=excluded.value"},
    {"clear", "DELETE FROM records WHERE database_name=? AND store_name=?"},
    {"indexes", "SELECT name,key_path,is_unique,multi_entry FROM indexes WHERE database_name=? "
                "AND store_name=? ORDER BY name"},
    {"createIndex", "INSERT INTO indexes VALUES(?,?,?,?,?,?)"},
    {"deleteIndex", "DELETE FROM indexes WHERE database_name=? AND store_name=? AND name=?"},
    {"renameIndex", "UPDATE indexes SET name=? WHERE database_name=? AND store_name=? AND name=?"},
    {"clearIndexKey", "DELETE FROM index_records WHERE database_name=? AND store_name=? "
                      "AND primary_key=?"},
    {"addIndexKey", "INSERT INTO index_records VALUES(?,?,?,?,?,?) ON CONFLICT"
                    "(database_name,store_name,index_name,key,primary_key) DO NOTHING"}};
}  // namespace
JSValue js_task_generation(JSContext* context, JSValueConst, int, JSValueConst*) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  return JS_NewFloat64(context, static_cast<double>(state.scheduler.generation()));
}
JSValue js_indexed_db_codec(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "codec operation and value required");
  const auto operation = js_string(context, argv[0]);
  if (operation == "decode") {
    size_t size = 0;
    auto* bytes = JS_GetArrayBuffer(context, &size, argv[1]);
    if (!bytes) return JS_EXCEPTION;
    return JS_ReadObject(context, bytes, size, JS_READ_OBJ_REFERENCE);
  }
  if (operation != "encode") return JS_ThrowTypeError(context, "invalid codec operation");
  size_t size = 0;
  auto* bytes = JS_WriteObject(context, &size, argv[1], JS_WRITE_OBJ_REFERENCE);
  if (!bytes) return JS_EXCEPTION;
  const auto release = [context](std::uint8_t* value) { js_free(context, value); };
  std::unique_ptr<std::uint8_t, decltype(release)> owned(bytes, release);
  if (size > HistoryArchive::kMaxBytes)
    return JS_ThrowRangeError(context, "stored value exceeds clone limit");
  return JS_NewArrayBufferCopy(context, bytes, size);
}
JSValue js_indexed_db(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* caller = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (caller->opaque_origin) return frame_security_error(context,"opaque frame origin has no IndexedDB");
  if (argc < 2 || !JS_IsArray(argv[1]))
    return JS_ThrowTypeError(context, "operation and parameter array required");
  const auto operation = js_string(context, argv[0]);
  const bool index_count = operation == "indexCount";
  const bool index_range = operation == "indexRows" || index_count;
  const bool cursor = operation == "cursorNext" || operation == "cursorPrevious" ||
                      operation == "cursorNextKey" || operation == "cursorPreviousKey";
  const bool range = cursor || operation == "get" || operation == "getKeys" ||
                     operation == "count" || operation == "delete";
  const auto found = statements.find(operation);
  if (!range && !index_range && found == statements.end())
    return JS_ThrowTypeError(context, "unknown storage operation");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state.indexed_db) {
    // The origin comes from native document state, never page-controlled URL.
    const auto origin = url_origin(state.history_source_url);
    if (origin.empty() || origin == "null")
      return JS_ThrowTypeError(context, "SecurityError: opaque origin");
    state.indexed_db = std::make_unique<IndexedDbStore>(origin);
  }
  const JsValueOwner length(context, JS_GetPropertyStr(context, argv[1], "length"));
  uint32_t supplied = 0;
  if (JS_ToUint32(context, &supplied, length.get()) < 0) return JS_EXCEPTION;
  std::string query;
  std::vector<uint32_t> parameters;
  if (index_range) {
    // [database, store, index, lower, lowerOpen, upper, upperOpen,
    //  reverse, unique, limit, offset, afterKey, afterPrimaryKey, inclusive]
    if (supplied != 14u) return JS_ThrowTypeError(context, "incorrect index parameter count");
    const auto boolean = [&](uint32_t index) {
      const JsValueOwner value(context, JS_GetPropertyUint32(context, argv[1], index));
      return JS_ToBool(context, value.get()) > 0;
    };
    const bool reverse = boolean(7), unique = boolean(8);
    query = index_count ? "SELECT count(*) FROM index_records i WHERE "
                          "i.database_name=? AND i.store_name=? AND i.index_name=?" :
            "SELECT i.key,i.primary_key,r.value FROM index_records i JOIN records r ON "
            "r.database_name=i.database_name AND r.store_name=i.store_name AND r.key=i.primary_key "
            "WHERE i.database_name=? AND i.store_name=? AND i.index_name=?";
    parameters = {0, 1, 2};
    for (uint32_t bound : {3u, 5u}) {
      const JsValueOwner value(context, JS_GetPropertyUint32(context, argv[1], bound));
      if (JS_IsNull(value.get()) || JS_IsUndefined(value.get())) continue;
      query += bound == 3 ? (boolean(4) ? " AND i.key>?" : " AND i.key>=?")
                         : (boolean(6) ? " AND i.key<?" : " AND i.key<=?");
      parameters.push_back(bound);
    }
    const JsValueOwner after(context, JS_GetPropertyUint32(context, argv[1], 11));
    if (!JS_IsNull(after.get()) && !JS_IsUndefined(after.get())) {
      const JsValueOwner primary(context, JS_GetPropertyUint32(context, argv[1], 12));
      const bool tuple = !JS_IsNull(primary.get()) && !JS_IsUndefined(primary.get());
      query += tuple ? " AND (i.key,i.primary_key)" : " AND i.key";
      query += reverse ? (boolean(13) ? "<=" : "<") : (boolean(13) ? ">=" : ">");
      query += tuple ? "(?,?)" : "?";
      parameters.push_back(11);
      if (tuple) parameters.push_back(12);
    }
    if (unique) {
      // Unique cursors use the lowest primary key for each index key in both directions.
      query += " AND i.primary_key=(SELECT min(j.primary_key) FROM index_records j WHERE "
               "j.database_name=i.database_name AND j.store_name=i.store_name "
               "AND j.index_name=i.index_name AND j.key=i.key)";
    }
    if (!index_count) {
      query += reverse ? " ORDER BY i.key DESC,i.primary_key DESC" : " ORDER BY i.key,i.primary_key";
      query += " LIMIT ? OFFSET ?";
      parameters.push_back(9);
      parameters.push_back(10);
    }
  } else if (range) {
    const bool records = operation != "count" && operation != "delete";
    if (supplied != (records ? 11u : 10u))
      return JS_ThrowTypeError(context, "incorrect range parameter count");
    query =
        operation == "delete"  ? "DELETE FROM records"
        : operation == "count" ? "SELECT count(*) FROM records"
        : operation == "getKeys" || operation == "cursorNextKey" || operation == "cursorPreviousKey"
            ? "SELECT key,NULL FROM records"
            : "SELECT key,value FROM records";
    query += " WHERE database_name=? AND store_name=?";
    parameters = {0, 1};
    // A nullable OR prevents primary-key seeks. Only fixed comparison fragments
    // enter SQL; values remain bound parameters, including cursor positions.
    for (uint32_t bound : {2u, 6u}) {
      const JsValueOwner value(context, JS_GetPropertyUint32(context, argv[1], bound));
      if (JS_IsException(value.get())) return JS_EXCEPTION;
      if (JS_IsNull(value.get()) || JS_IsUndefined(value.get())) continue;
      const JsValueOwner open(context, JS_GetPropertyUint32(context, argv[1], bound + 2));
      if (JS_IsException(open.get())) return JS_EXCEPTION;
      const int exclusive = JS_ToBool(context, open.get());
      if (exclusive < 0) return JS_EXCEPTION;
      query += bound == 2 ? (exclusive ? " AND key>?" : " AND key>=?")
                          : (exclusive ? " AND key<?" : " AND key<=?");
      parameters.push_back(bound);
    }
    if (records) {
      query += operation == "cursorPrevious" || operation == "cursorPreviousKey"
                   ? " ORDER BY key DESC"
                   : " ORDER BY key";
      query += cursor ? " LIMIT 1 OFFSET ?" : " LIMIT ?";
      parameters.push_back(10);
    }
  } else
    query = found->second;
  auto statement = state.indexed_db->prepare(query.c_str());
  const int count = sqlite3_bind_parameter_count(statement.get());
  if (!range && !index_range)
    for (int i = 0; i < count; ++i) parameters.push_back(i);
  if (!range && !index_range && supplied != static_cast<uint32_t>(count))
    return JS_ThrowTypeError(context, "incorrect parameter count");
  for (int i = 0; i < count; ++i) {
    const JsValueOwner value(context, JS_GetPropertyUint32(context, argv[1], parameters[i]));
    if (JS_IsException(value.get())) return JS_EXCEPTION;
    int status;
    if (JS_IsNull(value.get()) || JS_IsUndefined(value.get()))
      status = sqlite3_bind_null(statement.get(), i + 1);
    else if (JS_IsString(value.get())) {
      const JsCString text(context, value.get());
      status = sqlite3_bind_text64(statement.get(), i + 1, text.get(), text.size(),
                                   SQLITE_TRANSIENT, SQLITE_UTF8);
    } else if (JS_IsNumber(value.get()) || JS_IsBool(value.get())) {
      int64_t number = 0;
      if (JS_ToInt64(context, &number, value.get()) < 0) return JS_EXCEPTION;
      status = sqlite3_bind_int64(statement.get(), i + 1, number);
    } else {
      size_t size = 0;
      auto* bytes = JS_GetArrayBuffer(context, &size, value.get());
      if (!bytes) return JS_EXCEPTION;
      status = sqlite3_bind_blob64(statement.get(), i + 1, bytes, size, SQLITE_TRANSIENT);
    }
    state.indexed_db->check(status);
  }
  JsValueOwner rows(context, JS_NewArray(context));
  if (JS_IsException(rows.get())) return JS_EXCEPTION;
  uint32_t row_index = 0;
  for (;;) {
    const int status = sqlite3_step(statement.get());
    state.indexed_db->check(status);
    if (status == SQLITE_DONE) break;
    JsValueOwner row(context, JS_NewArray(context));
    if (JS_IsException(row.get())) return JS_EXCEPTION;
    for (int col = 0; col < sqlite3_column_count(statement.get()); ++col) {
      JSValue value;
      switch (sqlite3_column_type(statement.get(), col)) {
        case SQLITE_NULL:
          value = JS_NULL;
          break;
        case SQLITE_INTEGER:
          value = JS_NewFloat64(context,
                                static_cast<double>(sqlite3_column_int64(statement.get(), col)));
          break;
        case SQLITE_BLOB:
          value = JS_NewArrayBufferCopy(
              context, static_cast<const uint8_t*>(sqlite3_column_blob(statement.get(), col)),
              sqlite3_column_bytes(statement.get(), col));
          break;
        default:
          value = JS_NewStringLen(
              context, reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), col)),
              sqlite3_column_bytes(statement.get(), col));
          break;
      }
      if (JS_IsException(value) || JS_SetPropertyUint32(context, row.get(), col, value) < 0)
        return JS_EXCEPTION;
    }
    if (JS_SetPropertyUint32(context, rows.get(), row_index++, row.release()) < 0)
      return JS_EXCEPTION;
  }
  return rows.release();
}
}  // namespace webcli
