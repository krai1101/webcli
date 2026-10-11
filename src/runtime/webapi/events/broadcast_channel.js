(() => {
  const channels = new Map(), states = new WeakMap();
  const state = channel => {
    const value = states.get(channel);
    if (!value) throw new TypeError('BroadcastChannel required');
    return value;
  };
  class BroadcastChannel extends EventTarget {
    constructor(name) {
      if (!arguments.length) throw new TypeError('Channel name required');
      super();
      name = String(name);
      const id = __webcliBroadcast('create', name);
      states.set(this, {id, name, closed: false});
      channels.set(id, this);
      this.onmessage = this.onmessageerror = null;
    }
    get name() { return state(this).name; }
    postMessage(message) {
      const value = state(this);
      if (value.closed) throw new DOMException('Channel is closed', 'InvalidStateError');
      if (!arguments.length) throw new TypeError('Message required');
      __webcliBroadcast('send', value.id,
          __historyValue.pack(__cloneHistoryState(message).value));
    }
    close() {
      const value = state(this);
      if (value.closed) return;
      __webcliBroadcast('close', value.id);
      channels.delete(value.id);
      value.closed = true;
    }
  }
  globalThis.BroadcastChannel = BroadcastChannel;
  globalThis.__webcliDeliverBroadcast = (id, data, failed) => {
    const target = channels.get(id);
    if (!target) return;
    if (!failed) {
      try { data = __historyValue.unpack(data); }
      catch (_) { failed = true; data = null; }
    }
    target.dispatchEvent(new MessageEvent(failed ? 'messageerror' : 'message',
        {data: failed ? null : data, origin: location.origin}));
  };
})();
