// Query lists retain identity while URL setters replace their contents.
const __queryLists = (() => {
  const states = new WeakMap();
  const scalar = value => {
    if (typeof value === 'symbol')
      throw new TypeError('Cannot convert Symbol to string');
    const text = String(value);
    if (!/[\uD800-\uDFFF]/.test(text)) return text;
    let output = '';
    for (const point of text)
      output += point.length === 1 && /[\uD800-\uDFFF]/.test(point) ? '\uFFFD' :
                                                                      point;
    return output;
  };
  const state = receiver => {
    const value = states.get(receiver);
    if (!value) throw new TypeError('Illegal URLSearchParams invocation');
    return value;
  };
  const decode = text => {
    const input = new TextEncoder().encode(text.replace(/\+/g, ' '));
    let length = 0;
    const hex = byte => byte >= 48 && byte <= 57 ? byte - 48 :
        byte >= 65 && byte <= 70                 ? byte - 55 :
        byte >= 97 && byte <= 102                ? byte - 87 :
                                                   -1;
    for (let i = 0; i < input.length; ++i) {
      const high = hex(input[i + 1]), low = hex(input[i + 2]);
      if (input[i] === 37 && high >= 0 && low >= 0) {
        input[length++] = high * 16 + low;
        i += 2;
      } else
        input[length++] = input[i];
    }
    return new TextDecoder('utf-8', {ignoreBOM: true})
        .decode(input.subarray(0, length));
  };
  const parse = input => {
    const result = [];
    for (const field of scalar(input).replace(/^\?/, '').split('&')) {
      if (!field) continue;
      const index = field.indexOf('=');
      result.push([
        decode(index < 0 ? field : field.slice(0, index)),
        decode(index < 0 ? '' : field.slice(index + 1))
      ]);
    }
    return result;
  };
  const encode = text =>
      encodeURIComponent(text)
          .replace(
              /[!'()~]/g,
              value => '%' + value.charCodeAt(0).toString(16).toUpperCase())
          .replace(/%20/g, '+');
  const serialize = pairs =>
      pairs.map(([key, value]) => encode(key) + '=' + encode(value)).join('&');
  const update = current => {
    if (current.changed) current.changed(serialize(current.pairs));
  };
  const requireArgs = (count, minimum) => {
    if (count < minimum)
      throw new TypeError('Not enough URLSearchParams arguments');
  };
  const iterator = (receiver, kind) => {
    const current = state(receiver);
    let index = 0, done = false;
    return {
      next() {
        if (done || index >= current.pairs.length) {
          done = true;
          return {done: true, value: undefined};
        }
        const pair = current.pairs[index++];
        return {
          done: false,
          value: kind === 'entries' ? [...pair] : pair[kind === 'keys' ? 0 : 1]
        };
      },
      [Symbol.iterator]() {
        return this;
      },
      get[Symbol.toStringTag]() {
        return 'URLSearchParams Iterator';
      }
    };
  };
  globalThis.URLSearchParams = class URLSearchParams {
    constructor(input = '') {
      const current = {pairs: [], changed: null};
      states.set(this, current);
      if (input !== null &&
          (typeof input === 'object' || typeof input === 'function')) {
        const method = input[Symbol.iterator];
        if (method != null) {
          if (typeof method !== 'function')
            throw new TypeError('Invalid query iterator');
          // Use the originally read iterator method; getters run only once.
          for (const pair of {[Symbol.iterator]: () => method.call(input)}) {
            if (pair === null ||
                (typeof pair !== 'object' && typeof pair !== 'function'))
              throw new TypeError('Query entry must be a pair');
            const pairMethod = pair[Symbol.iterator];
            if (typeof pairMethod !== 'function')
              throw new TypeError('Query entry must be iterable');
            const values = Array.from(
                {[Symbol.iterator]: () => pairMethod.call(pair)}, scalar);
            if (values.length !== 2)
              throw new TypeError('Query entry must have two values');
            current.pairs.push(values);
          }
        } else {
          // Record conversion merges keys that become equal after USVString
          // normalization, preserving the first key's position and last value.
          const positions = new Map();
          for (const key of Reflect.ownKeys(input)) {
            if (!Object.getOwnPropertyDescriptor(input, key)?.enumerable)
              continue;
            const name = scalar(key), value = scalar(input[key]);
            if (positions.has(name))
              current.pairs[positions.get(name)][1] = value;
            else {
              positions.set(name, current.pairs.length);
              current.pairs.push([name, value]);
            }
          }
        }
      } else
        current.pairs = parse(input);
    }
    get size() {
      return state(this).pairs.length;
    }
    append(name, value) {
      const current = state(this);
      requireArgs(arguments.length, 2);
      current.pairs.push([scalar(name), scalar(value)]);
      update(current);
    }
    delete(name, value) {
      const current = state(this);
      requireArgs(arguments.length, 1);
      name = scalar(name);
      const match = value !== undefined;
      if (match) value = scalar(value);
      current.pairs = current.pairs.filter(
          pair => pair[0] !== name || match && pair[1] !== value);
      update(current);
    }
    get(name) {
      const current = state(this);
      requireArgs(arguments.length, 1);
      name = scalar(name);
      return current.pairs.find(pair => pair[0] === name)?.[1] ?? null;
    }
    getAll(name) {
      const current = state(this);
      requireArgs(arguments.length, 1);
      name = scalar(name);
      return current.pairs.filter(pair => pair[0] === name)
          .map(pair => pair[1]);
    }
    has(name, value) {
      const current = state(this);
      requireArgs(arguments.length, 1);
      name = scalar(name);
      const match = value !== undefined;
      if (match) value = scalar(value);
      return current.pairs.some(
          pair => pair[0] === name && (!match || pair[1] === value));
    }
    set(name, value) {
      const current = state(this);
      requireArgs(arguments.length, 2);
      name = scalar(name);
      value = scalar(value);
      let found = false;
      current.pairs = current.pairs.filter(pair => {
        if (pair[0] !== name) return true;
        if (found) return false;
        pair[1] = value;
        found = true;
        return true;
      });
      if (!found) current.pairs.push([name, value]);
      update(current);
    }
    sort() {
      const current = state(this);
      current.pairs.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
      update(current);
    }
    entries() {
      return iterator(this, 'entries');
    }
    keys() {
      return iterator(this, 'keys');
    }
    values() {
      return iterator(this, 'values');
    }
    forEach(callback, thisArg) {
      const current = state(this);
      if (typeof callback !== 'function')
        throw new TypeError('Callback must be a function');
      for (let i = 0; i < current.pairs.length; ++i) {
        const [key, value] = current.pairs[i];
        callback.call(thisArg, value, key, this);
      }
    }
    toString() {
      return serialize(state(this).pairs);
    }
    [Symbol.iterator]() {
      return this.entries();
    }
    get[Symbol.toStringTag]() {
      return 'URLSearchParams';
    }
  };
  return Object.freeze({
    toScalarString: scalar,
    attach(params, changed) {
      state(params).changed = changed;
    },
    replace(params, query) {
      state(params).pairs = parse(query);
    }
  });
})();
