{
  const {get, state, token, fail, sql, valid} = __idb;
  const loadStores = name =>
      new Map(sql('stores', [name]).map(row => [row[0], {
                                          keyPath: row[1] === null ? null : JSON.parse(row[1]),
                                          automatic: !!row[2],
                                          indexes: __idb.loadIndexes(name, row[0])
                                        }]));
  const maybeClose = database => {
    const s = get(database);
    if (!s.closing || s.transactions.size) return;
    s.closed = true;
    const connections = __idb.connections.get(s.name);
    connections?.delete(database);
    if (connections && !connections.size) __idb.connections.delete(s.name);
    const watchers = __idb.closeWatch.get(s.name);
    if (watchers)
      for (const notify of [...watchers]) notify();
  };
  const schemaTransaction = database => {
    const s = get(database), tx = s.upgrade;
    if (!tx || get(tx).finished)
      fail('InvalidStateError', 'Schema changes require an upgrade transaction');
    valid(tx);
    return tx;
  };
  class IDBDatabase extends EventTarget {
    constructor(secret, name, version) {
      super();
      if (secret !== token) throw new TypeError('Illegal constructor');
      state.set(this, {
        name,
        version,
        stores: loadStores(name),
        transactions: new Set(),
        closed: false,
        closing: false,
        upgrade: null
      });
      const connections = __idb.connections.get(name) || new Set();
      connections.add(this);
      __idb.connections.set(name, connections);
      __eventExceptionHandlers.set(this, () => {
        for (const tx of get(this).transactions)
          __idb.abort(tx, new DOMException('Database callback threw an exception', 'AbortError'));
      });
    }
    get name() {
      return get(this).name;
    }
    get version() {
      return get(this).version;
    }
    get objectStoreNames() {
      return __idb.names(get(this).stores.keys());
    }
    close() {
      get(this).closing = true;
      maybeClose(this);
    }
    transaction(storeNames, mode = 'readonly', options = {}) {
      const s = get(this);
      if (s.closed || s.closing || s.upgrade) fail('InvalidStateError');
      mode = String(mode);
      if (!['readonly', 'readwrite'].includes(mode))
        throw new TypeError('Invalid transaction mode');
      if (options?.durability && !['default', 'strict', 'relaxed'].includes(options.durability))
        throw new TypeError('Invalid durability');
      const names = typeof storeNames === 'string' ? [storeNames] : Array.from(storeNames, String);
      if (!names.length) fail('InvalidAccessError');
      if (names.some(name => !s.stores.has(name))) fail('NotFoundError');
      return new __idb.IDBTransaction(token, this, [...new Set(names)].sort(), mode);
    }
    createObjectStore(name, options = {}) {
      const tx = schemaTransaction(this), s = get(this);
      name = String(name);
      if (s.stores.has(name)) fail('ConstraintError');
      let keyPath = options?.keyPath ?? null;
      if (keyPath !== null) {
        keyPath = Array.isArray(keyPath) ? keyPath.map(String) : String(keyPath);
        const paths = Array.isArray(keyPath) ? keyPath : [keyPath];
        const identifier = /^[$_\p{ID_Start}][$\u200c\u200d_\p{ID_Continue}]*$/u;
        if (paths.some(path => path !== '' && path.split('.').some(part => !identifier.test(part))))
          fail('SyntaxError');
      }
      const automatic = !!options?.autoIncrement;
      if (automatic && (keyPath === '' || Array.isArray(keyPath))) fail('InvalidAccessError');
      sql('createStore',
          [s.name, name, keyPath === null ? null : JSON.stringify(keyPath), Number(automatic)]);
      s.stores.set(name, {keyPath, automatic, indexes: new Map()});
      return tx.objectStore(name);
    }
    deleteObjectStore(name) {
      schemaTransaction(this);
      const s = get(this);
      name = String(name);
      if (!s.stores.has(name)) fail('NotFoundError');
      sql('deleteStore', [s.name, name]);
      s.stores.delete(name);
      get(s.upgrade).wrappers.delete(name);
    }
  }
  Object.assign(__idb, {IDBDatabase, maybeClose});
  globalThis.IDBDatabase = IDBDatabase;
}
