{
  const {get, state, token, fail, sql, schedule, generation, settle} = __idb;
  const valid = transaction => {
    const s = get(transaction);
    if (s.finished || s.aborted || s.committing || s.cloning || s.turn !== generation())
      fail('TransactionInactiveError');
    return s;
  };
  const enqueue = (transaction, source, operation, previous = null) => {
    const s = valid(transaction),
          request = previous || new __idb.IDBRequest(token, source, transaction);
    Object.assign(get(request), {readyState: 'pending', result: undefined, error: null});
    s.queue.push({request, operation});
    __eventExceptionHandlers.set(
        request,
        () => abort(
            transaction, new DOMException('Request callback threw an exception', 'AbortError')));
    return request;
  };
  const finish = (transaction, error) => {
    const s = get(transaction);
    if (s.finished) return;
    s.finished = true;
    s.error = error;
    if (s.owned) {
      s.owned = false;
      __idb.release(transaction);
    }
    const db = get(s.db);
    db.transactions.delete(transaction);
    if ((error || s.aborted) && s.mode === 'versionchange') {
      db.version = s.oldVersion;
      db.stores = s.oldStores;
    }
    transaction.dispatchEvent(
        new Event(error || s.aborted ? 'abort' : 'complete', {bubbles: !!error || s.aborted}));
    if (s.onfinish)
      s.onfinish(
          error || (s.aborted ? new DOMException('Transaction aborted', 'AbortError') : null));
    __idb.maybeClose(s.db);
  };
  const drain = transaction => {
    const s = get(transaction);
    if (s.finished) return;
    if (s.aborted) {
      const item = s.queue.shift();
      if (item) {
        settle(item.request, undefined, new DOMException('Transaction aborted', 'AbortError'));
        schedule(() => drain(transaction), 0);
      } else
        finish(transaction, s.error);
      return;
    }
    const item = s.queue.shift();
    if (!item) {
      try {
        sql('commit');
      } catch (error) {
        abort(transaction, error);
        return;
      }
      finish(transaction, null);
      return;
    }
    let result;
    try {
      sql('save');
      result = item.operation();
      sql('release');
    } catch (error) {
      try {
        sql('undo');
        sql('release');
      } catch (_) {
        abort(transaction, error);
      }
      s.turn = generation();
      if (settle(item.request, undefined, error) && !s.aborted) abort(transaction, error);
      schedule(() => drain(transaction), 0);
      return;
    }
    s.turn = generation();
    settle(item.request, result);
    schedule(() => drain(transaction), 0);
  };
  const abort = (transaction, error = null) => {
    const s = get(transaction);
    if (s.finished || s.aborted) return;
    s.aborted = true;
    s.error = error;
    if (s.begun) {
      s.begun = false;
      try {
        sql('rollback');
      } catch (failure) {
        s.error ||= failure;
      }
    }
    // A parked transaction is drained only once it owns the gate. Its queued
    // requests must still receive asynchronous AbortError events.
    if (s.owned) schedule(() => drain(transaction), 0);
  };
  class IDBTransaction extends EventTarget {
    constructor(secret, db, stores, mode, upgrade = false) {
      super();
      if (secret !== token) throw new TypeError('Illegal constructor');
      const d = get(db);
      state.set(this, {
        db,
        stores,
        mode,
        queue: [],
        wrappers: new Map(),
        turn: generation(),
        cloning: false,
        committing: false,
        aborted: false,
        finished: false,
        owned: upgrade,
        begun: upgrade,
        error: null,
        oldVersion: d.version,
        oldStores: new Map([...d.stores].map(([name, metadata]) =>
            [name, {...metadata, indexes: new Map([...metadata.indexes].map(([key, value]) =>
                [key, {...value}]))}]))
      });
      d.transactions.add(this);
      __eventParents.set(this, db);
      __eventExceptionHandlers.set(
          this,
          () => abort(
              this, new DOMException('Transaction callback threw an exception', 'AbortError')));
      if (!upgrade)
        __idb.acquire(this, () => {
          const s = get(this);
          s.owned = true;
          try {
            if (!s.aborted) {
              sql('begin');
              s.begun = true;
            }
          } catch (error) {
            abort(this, error);
          }
          drain(this);
        });
    }
    get db() {
      return get(this).db;
    }
    get mode() {
      return get(this).mode;
    }
    get durability() {
      return 'default';
    }
    get error() {
      return get(this).error;
    }
    get objectStoreNames() {
      const s = get(this);
      return __idb.names(s.mode === 'versionchange' ? get(s.db).stores.keys() : s.stores);
    }
    objectStore(name) {
      const s = get(this);
      if (s.finished) fail('InvalidStateError');
      name = String(name);
      if (!get(s.db).stores.has(name) || (s.mode !== 'versionchange' && !s.stores.includes(name)))
        fail('NotFoundError');
      if (!s.wrappers.has(name)) s.wrappers.set(name, new __idb.IDBObjectStore(token, this, name));
      return s.wrappers.get(name);
    }
    abort() {
      const s = get(this);
      if (s.finished || s.aborted) fail('InvalidStateError');
      abort(this);
    }
    commit() {
      const s = valid(this);
      s.committing = true;
    }
  }
  Object.assign(__idb, {IDBTransaction, valid, enqueue, abort, drain});
  globalThis.IDBTransaction = IDBTransaction;
}
