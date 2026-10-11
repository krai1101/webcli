{
  const {
    get,
    state,
    token,
    fail,
    sql,
    valid,
    enqueue,
    encodeKey,
    decodeKey,
    range,
    encodeValue,
    decodeValue
  } = __idb;
  const inspect = (store, write = false) => {
    const s = get(store), transaction = valid(s.transaction), db = get(transaction.db);
    const metadata = db.stores.get(s.name);
    if (!metadata || metadata !== s.metadata) fail('InvalidStateError', 'Object store was deleted');
    if (write && transaction.mode === 'readonly') fail('ReadOnlyError');
    return {s, transaction, db, metadata};
  };
  const pathValue = (value, path) => {
    if (Array.isArray(path)) return path.map(item => pathValue(value, item));
    if (path === '') return value;
    for (const part of path.split('.')) {
      if (value == null || !(part in Object(value))) return undefined;
      value = value[part];
    }
    return value;
  };
  const inject = (value, path, key) => {
    const parts = path.split('.');
    if (value === null || typeof value !== 'object')
      fail('DataError', 'Generated key cannot be injected');
    let object = value;
    for (const part of parts.slice(0, -1)) {
      if (!(part in object))
        Object.defineProperty(
            object, part, {value: {}, writable: true, enumerable: true, configurable: true});
      object = object[part];
      if (object === null || typeof object !== 'object') fail('DataError');
    }
    Object.defineProperty(
        object, parts[parts.length - 1],
        {value: key, writable: true, enumerable: true, configurable: true});
  };
  const write = (store, value, key, overwrite, source = store, expected = null) => {
    const {s, transaction, db, metadata} = inspect(store, true);
    if (metadata.keyPath !== null && key !== undefined)
      fail('DataError', 'Inline stores do not accept an explicit key');
    // Mark inactive during getters, as required by the structured clone rules.
    let snapshot;
    transaction.cloning = true;
    try {
      snapshot = encodeValue(value);
    } finally {
      transaction.cloning = false;
    }
    const copy = decodeValue(snapshot);
    let chosen = metadata.keyPath === null ? key : pathValue(copy, metadata.keyPath);
    let encoded = null;
    if (chosen !== undefined)
      encoded = encodeKey(chosen);
    else if (!metadata.automatic)
      fail('DataError', 'Object store requires a key');
    else if (metadata.keyPath !== null)
      inject(copy, metadata.keyPath, 1);  // Validate injection synchronously.
    if (expected && (!encoded || __idb.compareBytes(encoded, expected) !== 0))
      fail('DataError', 'Cursor updates must preserve the primary key');
    return enqueue(s.transaction, source, () => {
      if (!encoded) {
        chosen = sql('generator', [db.name, s.name])[0][0];
        if (chosen > Number.MAX_SAFE_INTEGER) fail('ConstraintError', 'Key generator exhausted');
        encoded = encodeKey(chosen);
        if (metadata.keyPath !== null) {
          inject(copy, metadata.keyPath, chosen);
          snapshot = encodeValue(copy);
        }
      }
      sql(overwrite ? 'put' : 'add', [db.name, s.name, encoded, snapshot]);
      __idb.reindex(db.name, s.name, metadata.indexes, copy, encoded);
      if (metadata.automatic && typeof chosen === 'number' && chosen >= 1)
        sql('advance',
            [Math.min(Math.floor(chosen) + 1, Number.MAX_SAFE_INTEGER + 1), db.name, s.name]);
      return decodeKey(encoded);
    });
  };
  const read = (store, query, count, keys, multiple) => {
    const {s, db} = inspect(store);
    const bounds = range(query, !multiple);
    if (count !== undefined &&
        (!Number.isInteger(Number(count)) || Number(count) < 0 || Number(count) > 4294967295))
      throw new TypeError('Invalid record count');
    const limit = multiple ? (count === undefined || Number(count) === 0 ? -1 : Number(count)) : 1;
    return enqueue(s.transaction, store, () => {
      const result = sql(keys ? 'getKeys' : 'get', [
                       db.name, s.name, ...bounds, limit
                     ]).map(row => keys ? decodeKey(row[0]) : decodeValue(row[1]));
      return multiple ? result : result[0];
    });
  };
  class IDBObjectStore {
    constructor(secret, transaction, name) {
      if (secret !== token) throw new TypeError('Illegal constructor');
      state.set(this, {transaction, name, metadata: get(get(transaction).db).stores.get(name),
                      indexes: new Map()});
    }
    get name() {
      return get(this).name;
    }
    get transaction() {
      return get(this).transaction;
    }
    get keyPath() {
      const path = get(this).metadata.keyPath;
      return Array.isArray(path) ? [...path] : path;
    }
    get autoIncrement() {
      return get(this).metadata.automatic;
    }
    get indexNames() {
      return __idb.names(get(this).metadata.indexes.keys());
    }
    add(value, key) {
      if (!arguments.length) throw new TypeError('Value required');
      return write(this, value, key, false);
    }
    put(value, key) {
      if (!arguments.length) throw new TypeError('Value required');
      return write(this, value, key, true);
    }
    get(query) {
      if (!arguments.length) throw new TypeError('Query required');
      return read(this, query, undefined, false, false);
    }
    getKey(query) {
      if (!arguments.length) throw new TypeError('Query required');
      return read(this, query, undefined, true, false);
    }
    getAll(query, count) {
      return read(this, query, count, false, true);
    }
    getAllKeys(query, count) {
      return read(this, query, count, true, true);
    }
    count(query) {
      const {s, db} = inspect(this), bounds = range(query);
      return enqueue(s.transaction, this, () => sql('count', [db.name, s.name, ...bounds])[0][0]);
    }
    delete(query) {
      if (!arguments.length) throw new TypeError('Query required');
      const {s, db} = inspect(this, true), bounds = range(query, true);
      return enqueue(s.transaction, this, () => {
        sql('delete', [db.name, s.name, ...bounds]);
      });
    }
    clear() {
      const {s, db} = inspect(this, true);
      return enqueue(s.transaction, this, () => {
        sql('clear', [db.name, s.name]);
      });
    }
    createIndex(name, keyPath, options) {
      if (arguments.length < 2) throw new TypeError('Index name and key path required');
      return __idb.createIndex(this, name, keyPath, options);
    }
    deleteIndex(name) {
      if (!arguments.length) throw new TypeError('Index name required');
      __idb.deleteIndex(this, name);
    }
    index(name) {
      if (!arguments.length) throw new TypeError('Index name required');
      return __idb.index(this, name);
    }
    openCursor(query, direction = 'next') {
      return __idb.openCursor(this, query, direction, false);
    }
    openKeyCursor(query, direction = 'next') {
      return __idb.openCursor(this, query, direction, true);
    }
  }
  Object.assign(__idb, {IDBObjectStore, inspectStore: inspect, writeStore: write, pathValue});
  globalThis.IDBObjectStore = IDBObjectStore;
}
