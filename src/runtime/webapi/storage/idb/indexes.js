// Persistent secondary indexes share the record transaction and its savepoints.
{
  const {get, state, token, fail, sql, enqueue, encodeKey, decodeKey, decodeValue,
         compareBytes, range, inspectStore, pathValue} = __idb;
  const loadIndexes = (database, store) => new Map(sql('indexes', [database, store]).map(row =>
      [row[0], {name: row[0], keyPath: JSON.parse(row[1]), unique: !!row[2], multiEntry: !!row[3]}]));
  const inspect = index => {
    const s = get(index), storeState = get(s.store), db = get(get(storeState.transaction).db);
    if (db.stores.get(storeState.name) !== storeState.metadata ||
        storeState.metadata.indexes.get(s.metadata.name) !== s.metadata)
      fail('InvalidStateError', 'Index was deleted');
    const store = inspectStore(s.store);
    return {...store, index: s.metadata};
  };
  const addEntries = (database, store, index, value, primary) => {
    const extracted = pathValue(value, index.keyPath);
    const values = index.multiEntry && Array.isArray(extracted) ? extracted : [extracted];
    const seen = new Set();
    for (const value of values) {
      let key;
      try { key = encodeKey(value); }
      catch (error) { if (error.name === 'DataError') continue; throw error; }
      const identity = Array.from(new Uint8Array(key), byte => byte.toString(16).padStart(2, '0')).join('');
      if (seen.has(identity)) continue;
      seen.add(identity);
      sql('addIndexKey', [database, store, index.name, key, primary, Number(index.unique)]);
    }
  };
  const reindex = (database, store, indexes, value, primary) => {
    sql('clearIndexKey', [database, store, primary]);
    for (const index of indexes.values()) addEntries(database, store, index, value, primary);
  };
  const schema = store => {
    // Deleted handles take precedence over transaction inactivity.
    const s = get(store), db = get(get(s.transaction).db);
    if (db.stores.get(s.name) !== s.metadata) fail('InvalidStateError', 'Store was deleted');
    if (get(s.transaction).mode !== 'versionchange') fail('InvalidStateError');
    return inspectStore(store);
  };
  const index = (store, name) => {
    const s = get(store), tx = get(s.transaction), db = get(tx.db);
    if (db.stores.get(s.name) !== s.metadata || tx.finished) fail('InvalidStateError');
    name = String(name);
    const metadata = s.metadata.indexes.get(name);
    if (!metadata) fail('NotFoundError');
    if (!s.indexes.has(metadata)) s.indexes.set(metadata, new IDBIndex(token, store, metadata));
    return s.indexes.get(metadata);
  };
  const createIndex = (store, name, keyPath, options = {}) => {
    const {s, db, metadata} = schema(store);
    name = String(name);
    if (metadata.indexes.has(name)) fail('ConstraintError');
    keyPath = Array.isArray(keyPath) ? keyPath.map(String) : String(keyPath);
    const paths = Array.isArray(keyPath) ? keyPath : [keyPath];
    const identifier = /^[$_\p{ID_Start}][$\u200c\u200d_\p{ID_Continue}]*$/u;
    if (paths.some(path => path !== '' && path.split('.').some(part => !identifier.test(part))))
      fail('SyntaxError');
    if (options != null && typeof options !== 'object' && typeof options !== 'function')
      throw new TypeError('Index options must be a dictionary');
    const unique = !!options?.unique, multiEntry = !!options?.multiEntry;
    if (multiEntry && Array.isArray(keyPath)) fail('InvalidAccessError');
    const info = {name, keyPath, unique, multiEntry};
    sql('createIndex', [db.name, s.name, name, JSON.stringify(keyPath), Number(unique), Number(multiEntry)]);
    metadata.indexes.set(name, info);
    // Backfill in bounded batches. Failure is delivered asynchronously and aborts
    // the upgrade; it never leaves a partially populated committed index.
    const request = enqueue(s.transaction, store, () => {
      if (metadata.indexes.get(info.name) !== info) return;
      let bounds = range();
      for (;;) {
        const rows = sql('get', [db.name, s.name, ...bounds, 64]);
        for (const row of rows) addEntries(db.name, s.name, info, decodeValue(row[1]), row[0]);
        if (rows.length < 64) break;
        const last = rows[rows.length - 1][0];
        bounds = [last, last, 1, last, null, null, 0, null];
      }
    });
    request.addEventListener('error', () => __idb.abort(s.transaction, request.error));
    return index(store, name);
  };
  const deleteIndex = (store, name) => {
    const {s, db, metadata} = schema(store);
    name = String(name);
    if (!metadata.indexes.has(name)) fail('NotFoundError');
    sql('deleteIndex', [db.name, s.name, name]);
    s.indexes.delete(metadata.indexes.get(name));
    metadata.indexes.delete(name);
  };
  const rows = (index, bounds, direction = 'next', limit = -1, skip = 0,
                after = null, primary = null, inclusive = false) => {
    const s = get(index), store = get(s.store), db = get(get(store.transaction).db);
    return sql('indexRows', [db.name, store.name, s.metadata.name, bounds[0], bounds[2],
        bounds[4], bounds[6], Number(direction.startsWith('prev')), Number(direction.endsWith('unique')),
        limit, skip, after, primary, Number(inclusive)]);
  };
  const read = (index, query, count, keyOnly, multiple) => {
    const {s} = inspect(index), bounds = range(query, !multiple);
    if (count !== undefined &&
        (!Number.isInteger(Number(count)) || Number(count) < 0 || Number(count) > 4294967295))
      throw new TypeError('Invalid record count');
    const limit = multiple ? (count === undefined || Number(count) === 0 ? -1 : Number(count)) : 1;
    return enqueue(s.transaction, index, () => {
      const result = rows(index, bounds, 'next', limit).map(row =>
          keyOnly ? decodeKey(row[1]) : decodeValue(row[2]));
      return multiple ? result : result[0];
    });
  };
  class IDBIndex {
    constructor(secret, store, metadata) {
      if (secret !== token) throw new TypeError('Illegal constructor');
      state.set(this, {store, metadata});
    }
    get name() { return get(this).metadata.name; }
    set name(value) {
      const s = get(this), {db, metadata} = schema(s.store);
      if (metadata.indexes.get(s.metadata.name) !== s.metadata) fail('InvalidStateError');
      const name = String(value), old = s.metadata.name;
      if (name === old) return;
      if (metadata.indexes.has(name)) fail('ConstraintError');
      sql('renameIndex', [name, db.name, get(s.store).name, old]);
      metadata.indexes.delete(old);
      s.metadata.name = name;
      metadata.indexes.set(name, s.metadata);
    }
    get objectStore() { return get(this).store; }
    get keyPath() {
      const path = get(this).metadata.keyPath;
      return Array.isArray(path) ? [...path] : path;
    }
    get unique() { return get(this).metadata.unique; }
    get multiEntry() { return get(this).metadata.multiEntry; }
    get(query) {
      if (!arguments.length) throw new TypeError('Query required');
      return read(this, query, undefined, false, false);
    }
    getKey(query) {
      if (!arguments.length) throw new TypeError('Query required');
      return read(this, query, undefined, true, false);
    }
    getAll(query, count) { return read(this, query, count, false, true); }
    getAllKeys(query, count) { return read(this, query, count, true, true); }
    count(query) {
      const {s, db, index} = inspect(this), bounds = range(query);
      return enqueue(s.transaction, this, () => sql('indexCount', [db.name, s.name, index.name,
          bounds[0], bounds[2], bounds[4], bounds[6], 0, 0, -1, 0, null, null, 0])[0][0]);
    }
    openCursor(query, direction = 'next') { return __idb.openIndexCursor(this, query, direction, false); }
    openKeyCursor(query, direction = 'next') { return __idb.openIndexCursor(this, query, direction, true); }
  }
  Object.assign(__idb, {loadIndexes, reindex, createIndex, deleteIndex, index,
                       inspectIndex: inspect, indexRows: rows});
  globalThis.IDBIndex = IDBIndex;
}
