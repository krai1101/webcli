(() => {
  const access = __webcliWorker, states = new WeakMap(), targets = new Map();
  const pack = value => __historyValue.pack(__cloneHistoryState(value).value);
  const transferList = options => {
    const list = Array.isArray(options) ? options : options?.transfer ?? [];
    if (Array.from(list).length)
      throw new DOMException('Worker transfer lists are unsupported', 'NotSupportedError');
  };
  globalThis.Worker = class Worker extends EventTarget {
    constructor(url, options = {}) {
      super();
      if (!arguments.length) throw new TypeError('Worker URL required');
      const type = String(options?.type ?? 'classic');
      if (!['classic', 'module'].includes(type)) throw new TypeError('Invalid worker type');
      const credentials = String(options?.credentials ?? 'same-origin');
      if (!['omit', 'same-origin', 'include'].includes(credentials))
        throw new TypeError('Invalid worker credentials mode');
      if (type === 'module' && credentials !== 'same-origin')
        throw new DOMException('Module worker credentials overrides are unsupported', 'NotSupportedError');
      const resolved = new URL(String(url), document.baseURI).href;
      const id = access('create', resolved, type, String(options?.name ?? ''));
      states.set(this, id);
      targets.set(id, this);
    }
    postMessage(message, options = []) {
      if (!states.has(this)) throw new TypeError('Illegal Worker invocation');
      if (!arguments.length) throw new TypeError('Message required');
      transferList(options);
      access('send', states.get(this), pack(message));
    }
    terminate() {
      if (!states.has(this)) throw new TypeError('Illegal Worker invocation');
      const id = states.get(this);
      access('terminate', id);
      targets.delete(id);
    }
  };
  globalThis.__webcliDeliverWorker = (id, value, error) => {
    const target = targets.get(id);
    if (!target) return;
    if (error) {
      const event = new Event('error', {cancelable: true});
      Object.assign(event, {message: error, filename: '', lineno: 0, colno: 0});
      target.dispatchEvent(event);
    } else {
      target.dispatchEvent(new MessageEvent('message', {data: __historyValue.unpack(value)}));
    }
  };
})();
