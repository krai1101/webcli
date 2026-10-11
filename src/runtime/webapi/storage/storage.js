function __storage(persistent) {
  return {
    get length() {
      return __webcliStorage(persistent, 'keys').length;
    },
    key(index) {
      return __webcliStorage(persistent, 'keys')[index] ?? null;
    },
    getItem(key) {
      return __webcliStorage(persistent, 'get', String(key));
    },
    setItem(key, value) {
      __webcliStorage(persistent, 'set', String(key), String(value));
    },
    removeItem(key) {
      __webcliStorage(persistent, 'remove', String(key));
    },
    clear() {
      __webcliStorage(persistent, 'clear');
    }
  };
}
globalThis.localStorage = __storage(true);
globalThis.sessionStorage = __storage(false);
// queueMicrotask is supplied by the native engine job queue, independently of
