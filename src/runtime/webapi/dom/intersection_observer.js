// Nonvisual intersections follow DOM connectivity and CSS visibility. There is
// no viewport clipping or scrolling geometry; rectangles are nonvisual estimates.
(() => {
  const observers = new Set(), states = new WeakMap(), entries = new WeakMap();
  const entryToken = {}, schedule = setTimeout, cancel = clearTimeout;
  const computedStyle = getComputedStyle;
  let pending = null;
  const state = observer => {
    const value = states.get(observer);
    if (!value) throw new TypeError('IntersectionObserver required');
    return value;
  };
  const margin = value => {
    if (typeof value === 'symbol') throw new TypeError('Margin must be a string');
    const parts = String(value).trim().split(/\s+/);
    if (parts.length > 4 || parts.some(part =>
        !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(px|%)$/i.test(part)))
      throw new DOMException('Margin requires one to four px or % lengths', 'SyntaxError');
    const values = parts.map(part => {
      const number = parseFloat(part);
      if (!Number.isFinite(number))
        throw new DOMException('Margin length must be finite', 'SyntaxError');
      return String(number) + (part.endsWith('%') ? '%' : 'px');
    });
    return [values[0], values[1] ?? values[0], values[2] ?? values[0],
      values[3] ?? values[1] ?? values[0]].join(' ');
  };
  const parent = node => node.parentElement || node.getRootNode()?.host;
  const intersects = (target, root) => {
    if (!target.isConnected || target.ownerDocument !== document) return false;
    if (root && (!root.isConnected || root.ownerDocument !== document && root !== document))
      return false;
    if (['hidden', 'collapse'].includes(computedStyle(target).visibility)) return false;
    let withinRoot = root === null || root.nodeType === 9;
    for (let node = target; node; node = parent(node)) {
      if (node === root) withinRoot = true;
      if (computedStyle(node).display === 'none') return false;
    }
    return withinRoot;
  };
  class IntersectionObserverEntry {
    constructor(token, data) {
      if (token !== entryToken) throw new TypeError('Illegal IntersectionObserverEntry constructor');
      entries.set(this, data);
    }
  }
  for (const name of ['time', 'target', 'rootBounds', 'boundingClientRect',
      'intersectionRect', 'intersectionRatio', 'isIntersecting'])
    Object.defineProperty(IntersectionObserverEntry.prototype, name, {
      enumerable: true, configurable: true,
      get() {
        const value = entries.get(this);
        if (!value) throw new TypeError('IntersectionObserverEntry required');
        return value[name];
      }
    });
  const sample = observer => {
    const s = state(observer);
    for (const [target, previous] of s.targets) {
      const visible = intersects(target, s.root), ratio = visible ? 1 : 0;
      const index = s.thresholds.findIndex(threshold => threshold > ratio);
      if (previous && previous.visible === visible && previous.index === index) continue;
      s.targets.set(target, {visible, index});
      s.records.push(new IntersectionObserverEntry(entryToken, {
        time: performance.now(), target,
        rootBounds: s.root ? new DOMRectReadOnly() :
          new DOMRectReadOnly(0, 0, __webcliViewport.width, __webcliViewport.height),
        boundingClientRect: new DOMRectReadOnly(), intersectionRect: new DOMRectReadOnly(),
        intersectionRatio: ratio, isIntersecting: visible
      }));
    }
  };
  const deliver = () => {
    pending = null;
    const batch = [...observers];
    for (const observer of batch) sample(observer);
    for (const observer of batch) {
      const s = state(observer), records = s.records;
      s.records = [];
      if (!records.length) continue;
      try {s.callback.call(observer, records, observer);} catch (error) {
        __webcliReportError('IntersectionObserver: ' + error +
          (error?.stack ? '\n' + error.stack : ''));
      }
    }
  };
  const scheduleChecks = () => {
    if (observers.size && pending === null) pending = schedule(deliver, 0);
  };
  const unregister = observer => {
    observers.delete(observer);
    if (!observers.size && pending !== null) {cancel(pending); pending = null;}
  };
  class IntersectionObserver {
    constructor(callback, options = {}) {
      if (typeof callback !== 'function') throw new TypeError('Observer callback required');
      options ??= {};
      if (typeof options !== 'object' && typeof options !== 'function')
        throw new TypeError('Observer options must be a dictionary');
      const root = options.root ?? null;
      if (root !== null && (!(root instanceof DomNode) || ![1, 9].includes(root.nodeType)))
        throw new TypeError('Observer root must be an Element or Document');
      const threshold = options.threshold ?? 0;
      const sequence = threshold !== null &&
        (typeof threshold === 'object' || typeof threshold === 'function');
      if (sequence && typeof threshold[Symbol.iterator] !== 'function')
        throw new TypeError('Observer thresholds must be a number or iterable');
      const thresholds = (sequence ? Array.from(threshold, value => +value) : [+threshold])
        .sort((a, b) => a - b);
      if (thresholds.some(value => !Number.isFinite(value) || value < 0 || value > 1))
        throw new RangeError('Observer thresholds must be between zero and one');
      if (!thresholds.length) thresholds.push(0);
      states.set(this, {callback, root,
        rootMargin: margin(options.rootMargin === undefined ? '0px' : options.rootMargin),
        thresholds: Object.freeze(thresholds), targets: new Map(), records: []});
    }
    get root() {return state(this).root;}
    get rootMargin() {return state(this).rootMargin;}
    get thresholds() {return state(this).thresholds;}
    observe(target) {
      const s = state(this);
      if (!(target instanceof Element)) throw new TypeError('observe requires an Element');
      if (s.targets.has(target)) return;
      s.targets.set(target, null);
      observers.add(this);
      scheduleChecks();
    }
    unobserve(target) {
      const s = state(this);
      if (!(target instanceof Element)) throw new TypeError('unobserve requires an Element');
      s.targets.delete(target);
      s.records = s.records.filter(entry => entry.target !== target);
      if (!s.targets.size) unregister(this);
    }
    disconnect() {
      const s = state(this);
      s.targets.clear();
      s.records = [];
      unregister(this);
    }
    takeRecords() {
      const s = state(this), records = s.records;
      s.records = [];
      return records;
    }
  }
  Object.assign(globalThis, {IntersectionObserver, IntersectionObserverEntry,
    __webcliScheduleIntersectionChecks: scheduleChecks});
  for (const constructor of [IntersectionObserver, IntersectionObserverEntry])
    Object.defineProperty(constructor.prototype, Symbol.toStringTag, {
      value: constructor.name, configurable: true
    });
})();
