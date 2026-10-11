(() => {
  const token = Symbol('History constructor');
  let restoration = 'auto', instance;
  const receiver = value => {
    if (value !== instance) throw new TypeError('Illegal invocation');
  };
  const change = (self, state, url, replace) => {
    receiver(self);
    let target = location.href;
    if (url != null) {
      const value = String(url);
      if (value !== '') target = __webcliResolve(document.baseURI, value);
    }
    if (__parseURL(target).origin !== location.origin)
      throw new DOMException('History URL must be same-origin', 'SecurityError');
    __historyEngine.commit(state, target, replace);
  };
  globalThis.History = class History {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
    }
    get length() {
      receiver(this);
      return __historyEngine.length;
    }
    get state() {
      receiver(this);
      return __historyEngine.state;
    }
    get scrollRestoration() {
      receiver(this);
      return restoration;
    }
    set scrollRestoration(value) {
      receiver(this);
      value = String(value);
      if (value === 'auto' || value === 'manual') restoration = value;
    }
    pushState(state, unused, url) {
      if (arguments.length < 2) throw new TypeError('State and unused arguments required');
      change(this, state, url, false);
    }
    replaceState(state, unused, url) {
      if (arguments.length < 2) throw new TypeError('State and unused arguments required');
      change(this, state, url, true);
    }
    go(delta = 0) {
      receiver(this);
      __historyEngine.traverse(Number(delta) >> 0);
    }
    back() {
      receiver(this);
      __historyEngine.traverse(-1);
    }
    forward() {
      receiver(this);
      __historyEngine.traverse(1);
    }
    get[Symbol.toStringTag]() {
      return 'History';
    }
  };
  instance = new History(token);
  Object.defineProperty(globalThis, 'history', {get: () => instance, configurable: true});
})();
