(() => {
  const access = __webcliFrameAccess;
  const own = access('self'), windows = new Map();
  function send(id, message, targetOrigin = '/', transfer = []) {
    if (arguments.length < 2) throw new TypeError('Message required');
    if (targetOrigin !== null && typeof targetOrigin === 'object') {
      transfer = targetOrigin.transfer ?? [];
      targetOrigin = targetOrigin.targetOrigin ?? '/';
    }
    if (Array.from(transfer).length)
      throw new DOMException('Transfer lists are unsupported', 'NotSupportedError');
    targetOrigin = String(targetOrigin);
    if (targetOrigin !== '*' && targetOrigin !== '/') {
      try {
        targetOrigin = __parseURL(targetOrigin).origin;
      } catch (_) {
        throw new DOMException('Invalid target origin', 'SyntaxError');
      }
      if (targetOrigin === 'null') throw new DOMException('Invalid target origin', 'SyntaxError');
    }
    return access('message', id, message, targetOrigin);
  }
  function frameWindow(id) {
    if (id < 0) return null;
    if (id === own) return globalThis;
    if (windows.has(id)) return windows.get(id);
    const proxy = new Proxy(Object.create(null), {
      get(_, key) {
        if (key === Symbol.toStringTag) return 'Window';
        if (key === 'window' || key === 'self' || key === 'frames') return proxy;
        if (key === 'parent' || key === 'top') return frameWindow(access(key, id));
        if (key === 'closed' || key === 'length') return access(key, id);
        if (key === 'postMessage') return (...args) => send(id, ...args);
        if (key === 'frameElement') return access('frame-element', id);
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return frameWindow(access('child', id, Number(key)));
        return access('get', id, key);
      },
      set(_, key, value) {
        if (key === 'location') {
          access('navigate', id, value);
          return true;
        }
        return !!access('set', id, key, value);
      },
      getPrototypeOf() {
        return null;
      },
      defineProperty() {
        throw new DOMException('Window proxy definitions are unsupported', 'NotSupportedError');
      }
    });
    windows.set(id, proxy);
    return proxy;
  }
  function refresh() {
    const count = access('length', own);
    for (let i = 0; i < count; i++)
      Object.defineProperty(
          globalThis, String(i),
          {configurable: true, get: () => frameWindow(access('child', own, i))});
    return count;
  }
  Object.defineProperties(globalThis, {
    parent: {configurable: true, get: () => frameWindow(access('parent', own))},
    top: {configurable: true, get: () => frameWindow(access('top', own))},
    frameElement: {configurable: true, get: () => access('frame-element', own)},
    frames: {
      configurable: true,
      get: () => {
        refresh();
        return globalThis;
      }
    },
    length: {configurable: true, get: refresh}
  });
  globalThis.postMessage = (...args) => send(own, ...args);
  const dispatch = EventTarget.prototype.dispatchEvent.bind(globalThis);
  globalThis.__webcliDeliverFrameMessage = (data, origin, source) =>
      dispatch(new MessageEvent('message', {data, origin, source: frameWindow(source)}));
  Object.defineProperties(HTMLIFrameElement.prototype, {
    contentWindow: {
      get() {
        return frameWindow(access('element-id', this.__id));
      }
    },
    contentDocument: {
      get() {
        const id = access('element-id', this.__id);
        return id < 0 ? null : access('document', id);
      }
    },
    srcdoc: {
      get() {
        return this.getAttribute('srcdoc') || '';
      },
      set(value) {
        this.setAttribute('srcdoc', String(value));
      }
    },
    name: {
      get() {
        return this.getAttribute('name') || '';
      },
      set(value) {
        this.setAttribute('name', String(value));
      }
    }
  });
})();
