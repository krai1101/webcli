{
  const {get, state, token, fail} = __idb;
  class IDBRequest extends EventTarget {
    constructor(secret, source = null, transaction = null) {
      super();
      if (secret !== token) throw new TypeError('Illegal constructor');
      state.set(this, {source, transaction, readyState: 'pending', result: undefined, error: null});
      if (transaction) __eventParents.set(this, transaction);
    }
    get result() {
      const s = get(this);
      if (s.readyState === 'pending') fail('InvalidStateError');
      return s.result;
    }
    get error() {
      const s = get(this);
      if (s.readyState === 'pending') fail('InvalidStateError');
      return s.error;
    }
    get source() {
      return get(this).source;
    }
    get transaction() {
      return get(this).transaction;
    }
    get readyState() {
      return get(this).readyState;
    }
  }
  class IDBOpenDBRequest extends IDBRequest {}
  class IDBVersionChangeEvent extends Event {
    constructor(type, options = {}) {
      super(type, options);
      Object.defineProperties(this, {
        oldVersion: {value: Number(options.oldVersion ?? 0), enumerable: true},
        newVersion: {
          value: options.newVersion == null ? null : Number(options.newVersion),
          enumerable: true
        }
      });
    }
  }
  const settle = (request, result, error = null) => {
    Object.assign(get(request), {readyState: 'done', result, error});
    return request.dispatchEvent(
        new Event(error ? 'error' : 'success', {bubbles: !!error, cancelable: !!error}));
  };
  Object.assign(__idb, {settle, IDBRequest, IDBOpenDBRequest, IDBVersionChangeEvent});
  Object.assign(globalThis, {IDBRequest, IDBOpenDBRequest, IDBVersionChangeEvent});
}
