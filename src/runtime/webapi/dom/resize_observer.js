// WebCLI has no layout boxes: every observed box has the initial 0 x 0 size.
// ResizeObserver reports changes from that initial size, so no entries become
// active in this geometry model. Keep registration and receiver semantics for
// nonvisual consumers without fabricating dimensions or resize callbacks.
(() => {
  const states = new WeakMap();
  const state = observer => {
    const value = states.get(observer);
    if (!value) throw new TypeError('ResizeObserver required');
    return value;
  };
  class ResizeObserver {
    constructor(callback) {
      if (typeof callback !== 'function') throw new TypeError('Observer callback required');
      states.set(this, {callback, targets: new Map()});
    }
    observe(target, options = {}) {
      const s = state(this);
      if (!(target instanceof Element)) throw new TypeError('observe requires an Element');
      options ??= {};
      if (typeof options !== 'object' && typeof options !== 'function')
        throw new TypeError('Observer options must be a dictionary');
      const requested = options.box === undefined ? 'content-box' : options.box;
      if (typeof requested === 'symbol') throw new TypeError('Invalid observed box');
      const box = String(requested);
      if (!['content-box', 'border-box', 'device-pixel-content-box'].includes(box))
        throw new TypeError('Invalid observed box');
      s.targets.set(target, box);
    }
    unobserve(target) {
      const s = state(this);
      if (!(target instanceof Element)) throw new TypeError('unobserve requires an Element');
      s.targets.delete(target);
    }
    disconnect() {
      state(this).targets.clear();
    }
  }
  globalThis.ResizeObserver = ResizeObserver;
  Object.defineProperty(ResizeObserver.prototype, Symbol.toStringTag, {
    value: 'ResizeObserver', configurable: true
  });
})();
