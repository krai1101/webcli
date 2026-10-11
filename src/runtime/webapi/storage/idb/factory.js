{
  const {
    get,
    token,
    fail,
    sql,
    schedule,
    settle,
    acquire,
    release,
    connections,
    closeWatch,
    IDBVersionChangeEvent
  } = __idb;
  const checkOrigin = () => {
    if (location.origin === 'null') fail('SecurityError', 'IndexedDB requires a nonopaque origin');
  };
  const waitForConnections = (name, version, request, resume) => {
    const current = [...(connections.get(name) || [])];
    for (const db of current)
      if (!get(db).closing)
        db.dispatchEvent(new IDBVersionChangeEvent(
            'versionchange', {oldVersion: db.version, newVersion: version}));
    if (!(connections.get(name)?.size)) return false;
    request.dispatchEvent(new IDBVersionChangeEvent(
        'blocked', {oldVersion: current[0]?.version ?? 0, newVersion: version}));
    const watchers = closeWatch.get(name) || new Set();
    const notify = () => {
      if (connections.get(name)?.size) return;
      watchers.delete(notify);
      if (!watchers.size) closeWatch.delete(name);
      schedule(resume, 0);
    };
    watchers.add(notify);
    closeWatch.set(name, watchers);
    return true;
  };
  const open = (name, suppliedVersion) => {
    checkOrigin();
    name = String(name);
    let version;
    if (suppliedVersion !== undefined) {
      version = Number(suppliedVersion);
      if (!Number.isFinite(version) || version < 1 || version > Number.MAX_SAFE_INTEGER)
        throw new TypeError('Database version must be a positive safe integer');
      version = Math.trunc(version);
    }
    const request = new __idb.IDBOpenDBRequest(token);
    const start = () => acquire(request, () => {
      let db, tx;
      try {
        const oldVersion = sql('database', [name])[0]?.[0] ?? 0;
        const targetVersion = version ?? (oldVersion || 1);
        if (targetVersion < oldVersion) fail('VersionError');
        if (targetVersion > oldVersion && connections.get(name)?.size) {
          release(request);
          if (!waitForConnections(name, targetVersion, request, start)) start();
          return;
        }
        if (targetVersion === oldVersion) {
          db = new __idb.IDBDatabase(token, name, oldVersion);
          release(request);
          settle(request, db);
          return;
        }
        sql('begin');
        // Persisted schema changes and record requests share this transaction.
        db = new __idb.IDBDatabase(token, name, oldVersion);
        tx = new __idb.IDBTransaction(token, db, [], 'versionchange', true);
        __idb.transfer(request, tx);
        get(db).upgrade = tx;
        get(db).version = targetVersion;
        Object.assign(get(request), {result: db, transaction: tx, readyState: 'done'});
        __eventExceptionHandlers.set(
            request,
            () => __idb.abort(
                tx, new DOMException('Upgrade callback threw an exception', 'AbortError')));
        get(tx).onfinish = error => {
          get(db).upgrade = null;
          get(request).transaction = null;
          __eventExceptionHandlers.delete(request);
          if (error) db.close();
          schedule(
              () => settle(
                  request, error ? undefined : db,
                  error ? new DOMException('Database upgrade aborted', 'AbortError') : null),
              0);
        };
        sql('version', [name, targetVersion]);
        request.dispatchEvent(
            new IDBVersionChangeEvent('upgradeneeded', {oldVersion, newVersion: targetVersion}));
        schedule(() => __idb.drain(tx), 0);
      } catch (error) {
        if (tx)
          __idb.abort(tx, error);
        else {
          try {
            sql('rollback');
          } catch (_) {
          }
          db?.close();
          release(request);
          settle(request, undefined, error);
        }
      }
    });
    start();
    return request;
  };
  class IDBFactory {
    constructor(secret) {
      if (secret !== token) throw new TypeError('Illegal constructor');
    }
    open(name, version) {
      if (!arguments.length) throw new TypeError('Database name required');
      return open(name, version);
    }
    deleteDatabase(name) {
      if (!arguments.length) throw new TypeError('Database name required');
      checkOrigin();
      name = String(name);
      const request = new __idb.IDBOpenDBRequest(token);
      const start = () => acquire(request, () => {
        try {
          const oldVersion = sql('database', [name])[0]?.[0] ?? 0;
          if (connections.get(name)?.size) {
            release(request);
            if (!waitForConnections(name, null, request, start)) start();
            return;
          }
          sql('begin');
          sql('deleteDatabase', [name]);
          sql('commit');
          release(request);
          Object.assign(get(request), {result: undefined, readyState: 'done'});
          request.dispatchEvent(
              new IDBVersionChangeEvent('success', {oldVersion, newVersion: null}));
        } catch (error) {
          try {
            sql('rollback');
          } catch (_) {
          }
          release(request);
          settle(request, undefined, error);
        }
      });
      start();
      return request;
    }
    cmp(first, second) {
      if (arguments.length < 2) throw new TypeError('Two keys required');
      return __idb.compareBytes(__idb.encodeKey(first), __idb.encodeKey(second));
    }
    databases() {
      checkOrigin();
      return new Promise((resolve, reject) => {
        const identity = {};
        acquire(identity, () => {
          try {
            resolve(sql('databases').map(row => ({name: row[0], version: row[1]})));
          } catch (error) {
            reject(error);
          } finally {
            release(identity);
          }
        });
      });
    }
  }
  Object.assign(globalThis, {IDBFactory, indexedDB: new IDBFactory(token)});
}
