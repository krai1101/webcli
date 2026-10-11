// Native parser arenas live only for each operation. JS retains component
// strings privately, with a stable linked query list created on demand.
(() => {
  const values = new WeakMap(), parse = __webcliURLParse;
  const text = __queryLists.toScalarString;
  const state = value => {
    const current = values.get(value);
    if (!current) throw new TypeError('Illegal URL invocation');
    return current;
  };
  const inputs =
      (input,
       base) => [text(input), base === undefined ? undefined : text(base)];
  globalThis.URL = class URL {
    constructor(input, base) {
      if (!arguments.length) throw new TypeError('URL required');
      values.set(this, {parts: parse(...inputs(input, base)), params: null});
    }
    get searchParams() {
      const current = state(this);
      if (!current.params) {
        current.params = new URLSearchParams(current.parts.search);
        __queryLists.attach(current.params, query => {
          current.parts = parse(current.parts.href, undefined, 'search', query);
        });
      }
      return current.params;
    }
    toString() {
      return state(this).parts.href;
    }
    toJSON() {
      return state(this).parts.href;
    }
    static parse(input, base) {
      if (!arguments.length) throw new TypeError('URL required');
      const args = inputs(input, base);
      try {
        return new URL(...args);
      } catch (error) {
        if (error instanceof TypeError) return null;
        throw error;
      }
    }
    static canParse(input, base) {
      if (!arguments.length) throw new TypeError('URL required');
      const args = inputs(input, base);
      try {
        parse(...args);
        return true;
      } catch (error) {
        if (error instanceof TypeError) return false;
        throw error;
      }
    }
    get[Symbol.toStringTag]() {
      return 'URL';
    }
  };
  for (const name
           of ['href', 'origin', 'protocol', 'username', 'password', 'host',
               'hostname', 'port', 'pathname', 'search', 'hash']) {
    const descriptor = {
      enumerable: true,
      configurable: true,
      get() {
        return state(this).parts[name];
      }
    };
    if (name !== 'origin')
      descriptor.set = function(value) {
        const current = state(this), converted = text(value);
        current.parts = parse(current.parts.href, undefined, name, converted);
        if (current.params && (name === 'href' || name === 'search'))
          __queryLists.replace(current.params, current.parts.search);
      };
    Object.defineProperty(URL.prototype, name, descriptor);
  }
})();
