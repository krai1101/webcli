// DOMTokenList is live, ordered, deduplicated, and stable for an element.
const __tokenListNodes = new WeakMap(),
  __classLists = new WeakMap();

function __tokenNode(list) {
  const node = __tokenListNodes.get(list);
  if (!node) throw new TypeError('Illegal invocation');
  return node;
}

function __tokenItems(list) {
  return [...new Set((__tokenNode(list).getAttribute('class') || '').split(/[\t\n\f\r ]+/).filter(
    Boolean))];
}

function __validateToken(value) {
  const token = String(value);
  if (!token) throw new DOMException('Empty token', 'SyntaxError');
  if (/[\t\n\f\r ]/.test(token)) throw new DOMException('Whitespace in token',
    'InvalidCharacterError');
  return token;
}
class DOMTokenList {
  constructor() {
    throw new TypeError('Illegal constructor');
  }
  get length() {
    return __tokenItems(this).length;
  }
  get value() {
    return __tokenNode(this).getAttribute('class') || '';
  }
  set value(value) {
    __tokenNode(this).setAttribute('class', String(value));
  }
  item(index) {
    if (!arguments.length) throw new TypeError('Index required');
    return __tokenItems(this)[Number(index) >>> 0] ?? null;
  }
  contains(token) {
    if (!arguments.length) throw new TypeError('Token required');
    return __tokenItems(this).includes(__validateToken(token));
  }
  add(...values) {
    const tokens = values.map(__validateToken);
    this.value = [...new Set([...__tokenItems(this), ...tokens])].join(' ');
  }
  remove(...values) {
    const tokens = values.map(__validateToken);
    this.value = __tokenItems(this).filter(token => !tokens.includes(token)).join(' ');
  }
  toggle(value, force) {
    if (!arguments.length) throw new TypeError('Token required');
    const token = __validateToken(value),
      present = this.contains(token);
    if (present) {
      if (force === undefined || !force) {
        this.remove(token);
        return false;
      }
      return true;
    }
    if (force === undefined || force) {
      this.add(token);
      return true;
    }
    return false;
  }
  replace(value, replacement) {
    if (arguments.length < 2) throw new TypeError('Two tokens required');
    const token = __validateToken(value),
      other = __validateToken(replacement),
      items = __tokenItems(this);
    const index = items.indexOf(token);
    if (index < 0) return false;
    if (!items.includes(other)) items[index] = other;
    else if (other !== token) items.splice(index, 1);
    this.value = items.join(' ');
    return true;
  }
  supports() {
    throw new TypeError('classList has no supported-token vocabulary');
  }
  forEach(callback, thisArg) {
      if (typeof callback !== 'function') throw new TypeError('Callback required');
      for (let index = 0; index < this.length; ++index) callback.call(thisArg, this[index], index,
        this);
    }
    * values() {
      for (let index = 0; index < this.length; ++index) yield this[index];
    }
    * keys() {
      for (let index = 0; index < this.length; ++index) yield index;
    }
    * entries() {
      for (let index = 0; index < this.length; ++index) yield [index, this[index]];
    }
    [Symbol.iterator]() {
      return this.values();
    }
  toString() {
    return this.value;
  }
  get[Symbol.toStringTag]() {
    return 'DOMTokenList';
  }
}
globalThis.DOMTokenList = DOMTokenList;

function __classList(node) {
  if (!__classLists.has(node)) {
    const target = Object.create(DOMTokenList.prototype);
    const indexed = key => typeof key === 'string' && /^(0|[1-9]\d*)$/.test(key) && Number(key) <
      0xffffffff;
    const list = new Proxy(target, {
      get(target, key, receiver) {
        return indexed(key) ? __tokenItems(receiver)[Number(key)] : Reflect.get(target, key,
          receiver);
      },
      has(target, key) {
        return indexed(key) ? Number(key) < __tokenItems(target).length : Reflect.has(target,
          key);
      },
      set(target, key, value, receiver) {
        return indexed(key) ? false : Reflect.set(target, key, value, receiver);
      },
      defineProperty(target, key, descriptor) {
        return indexed(key) ? false : Reflect.defineProperty(target, key, descriptor);
      },
      deleteProperty(target, key) {
        return indexed(key) ? false : Reflect.deleteProperty(target, key);
      },
      preventExtensions() {
        return false;
      },
      ownKeys(target) {
        return [...__tokenItems(target).map((_, index) => String(index)), ...Reflect.ownKeys(
          target)];
      },
      getOwnPropertyDescriptor(target, key) {
        if (indexed(key)) {
          const value = __tokenItems(target)[Number(key)];
          return value === undefined ? undefined : {
            value,
            enumerable: true,
            configurable: true,
            writable: false
          };
        }
        return Reflect.getOwnPropertyDescriptor(target, key);
      }
    });
    __tokenListNodes.set(target, node);
    __tokenListNodes.set(list, node);
    __classLists.set(node, list);
  }
  return __classLists.get(node);
}
