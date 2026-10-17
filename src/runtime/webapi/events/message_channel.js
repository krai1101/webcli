(() => {
  const access = __webcliMessagePort, states = new WeakMap(), ports = new Map(), token = {};
  const state = port => {
    const value = states.get(port);
    if (!value) throw new TypeError('MessagePort required');
    return value;
  };
  class MessagePort extends EventTarget {
    constructor(key, id) {
      super();
      if (key !== token) throw new TypeError('Illegal constructor');
      states.set(this, {id, detached: false, handler: null, errorHandler: null});
      ports.set(id, this);
    }
    postMessage(message, options = []) {
      const value = state(this);
      if (!arguments.length) throw new TypeError('Message required');
      const transfer = Array.isArray(options) ? options : options?.transfer ?? [];
      if (Array.from(transfer).length)
        throw new DOMException('Nested port and buffer transfers are unsupported', 'NotSupportedError');
      const data = __historyValue.pack(__cloneHistoryState(message).value);
      if (!value.detached) access('send', value.id, data);
    }
    start() {
      const value = state(this);
      if (!value.detached) access('start', value.id);
    }
    close() {
      const value = state(this);
      if (!value.detached) access('close', value.id);
      ports.delete(value.id);
      value.detached = true;
    }
    get onmessage() { return state(this).handler; }
    set onmessage(handler) {
      state(this).handler = typeof handler === 'function' ? handler : null;
      if (state(this).handler) this.start();
    }
    get onmessageerror() { return state(this).errorHandler; }
    set onmessageerror(handler) {
      state(this).errorHandler = typeof handler === 'function' ? handler : null;
    }
  }
  class MessageChannel {
    constructor() {
      const ids = access('create');
      Object.defineProperties(this, {
        port1: {value: new MessagePort(token, ids[0]), enumerable: true},
        port2: {value: new MessagePort(token, ids[1]), enumerable: true}
      });
    }
  }
  // Detach only after serialization and native ownership transfer succeed.
  globalThis.__webcliPortTransfer = {
    ids(list) {
      const ids = Array.from(list, port => {
        const value = states.get(port);
        if (!value) throw new DOMException('Only message ports can be transferred', 'DataCloneError');
        if (value.detached) throw new DOMException('Port is detached', 'DataCloneError');
        return value.id;
      });
      if (new Set(ids).size !== ids.length)
        throw new DOMException('Duplicate transferred port', 'DataCloneError');
      return ids;
    },
    detach(list) {
      for (const port of list) {
        const value = state(port);
        ports.delete(value.id);
        value.detached = true;
      }
    },
    adopt(ids) { return Array.from(ids, id => new MessagePort(token, id)); }
  };
  globalThis.__webcliDeliverPort = (id, data, failed) => {
    const port = ports.get(id);
    if (!port) return;
    if (!failed) {
      try { data = __historyValue.unpack(data); }
      catch (_) { failed = true; }
    }
    __dispatchBrowserEvent.call(port,
        new MessageEvent(failed ? 'messageerror' : 'message', {data: failed ? null : data}), true);
  };
  for (const type of [MessagePort, MessageChannel])
    Object.defineProperty(type.prototype, Symbol.toStringTag, {value: type.name, configurable: true});
  Object.assign(globalThis, {MessagePort, MessageChannel});
})();
