(() => {
  const access = __webcliFrameAccess;
  const own = access('self'), windows = new Map();
  function send(id, message, targetOrigin = '/', transfer = []) {
    if (arguments.length < 2) throw new TypeError('Message required');
    if (targetOrigin !== null && typeof targetOrigin === 'object') {
      transfer = targetOrigin.transfer ?? [];
      targetOrigin = targetOrigin.targetOrigin ?? '/';
    }
    transfer = Array.from(transfer);
    const ids = __webcliPortTransfer.ids(transfer);
    targetOrigin = String(targetOrigin);
    if (targetOrigin !== '*' && targetOrigin !== '/') {
      try {
        targetOrigin = __parseURL(targetOrigin).origin;
      } catch (_) {
        throw new DOMException('Invalid target origin', 'SyntaxError');
      }
      if (targetOrigin === 'null') throw new DOMException('Invalid target origin', 'SyntaxError');
    }
    const result = access('message', id, message, targetOrigin, ids);
    __webcliPortTransfer.detach(transfer);
    return result;
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
        if (key === 'opener') return frameWindow(access('opener', id));
        if (key === 'closed' || key === 'length') return access(key, id);
        if (key === 'focus' || key === 'blur') return () => {};
        if (key === 'close') return () => access('close', id);
        if (key === 'postMessage') return (...args) => send(id, ...args);
        if (key === 'frameElement') return access('frame-element', id);
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return indexedWindow(id, Number(key));
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
  function indexedWindow(parent, index) {
    const id = access('child', parent, index);
    return id < 0 ? undefined : frameWindow(id);
  }
  globalThis.__webcliWindowProxy = frameWindow;
  let indexedCount = 0;
  function refresh(count = access('length', own)) {
    for (let i = count; i < indexedCount; i++) delete globalThis[String(i)];
    for (let i = indexedCount; i < count; i++)
      Object.defineProperty(
          globalThis, String(i),
          {configurable: true, get: () => indexedWindow(own, i)});
    indexedCount = count;
    return count;
  }
  globalThis.__webcliRefreshFrameIndices = refresh;
  Object.defineProperties(globalThis, {
    parent: {configurable: true, get: () => frameWindow(access('parent', own))},
    top: {configurable: true, get: () => frameWindow(access('top', own))},
    opener: {configurable: true, get: () => frameWindow(access('opener', own))},
    closed: {configurable: true, get: () => !!access('closed', own)},
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
  globalThis.open = (url = '', target = '_blank', features = '') => {
    const tokens = String(features).toLowerCase().split(/[ ,]+/);
    const noOpener = tokens.some(value => /^(noopener|noreferrer)(=(1|yes|true))?$/.test(value));
    const noReferrer = tokens.some(value => /^noreferrer(=(1|yes|true))?$/.test(value));
    const reserved = String(target).toLowerCase();
    if (['_self', '_parent', '_top'].includes(reserved)) {
      const destination = reserved === '_self' ? globalThis : reserved === '_parent' ? parent : top;
      destination.location = String(url) || 'about:blank';
      return noOpener ? null : destination;
    }
    const id = access('open', String(url), String(target), noOpener, noReferrer);
    return noOpener ? null : access('popup-window', id);
  };
  globalThis.close = () => access('close', own);
  globalThis.focus = globalThis.blur = () => {};
  const dispatch = __dispatchBrowserEvent;
  globalThis.__webcliDeliverFrameMessage = (data, origin, source, ports = []) =>
      dispatch.call(globalThis, new MessageEvent('message', {data, origin, source: frameWindow(source),
        ports: __webcliPortTransfer.adopt(ports)}), true);
  Object.defineProperties(HTMLIFrameElement.prototype, {
    sandbox: {
      configurable: true, enumerable: true,
      get() { return __attributeTokenList(this, 'sandbox'); },
      set(value) { this.setAttribute('sandbox', String(value)); }
    },
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
