// Document-local User Timing. Unsupported navigation/resource/rendering timing
// types have no entries; query methods never manufacture metrics for them.
(() => {
  const token = {}, states = new WeakMap(), origin = Date.now() - __webcliNow();
  let entries = [];
  const text = value => {
    if (typeof value === 'symbol') throw new TypeError('Cannot convert Symbol to string');
    return String(value);
  };
  const state = entry => {
    const result = states.get(entry);
    if (!result) throw new TypeError('Incompatible performance entry');
    return result;
  };
  const timestamp = value => {
    const result = +value;
    if (!Number.isFinite(result) || result < 0) throw new TypeError('Invalid timestamp');
    return result;
  };
  const detail = value => value === undefined ? null : __cloneHistoryState(value).value;
  const reserved = new Set(__legacyPerformanceTiming.fields);
  const syntax = message => new DOMException(message, 'SyntaxError');
  const boundary = value => {
    if (typeof value === 'number') return timestamp(value);
    const name = text(value);
    for (let i = entries.length - 1; i >= 0; --i)
      if (entries[i].entryType === 'mark' && entries[i].name === name) return entries[i].startTime;
    if (name === 'navigationStart') return 0;
    throw syntax('Unknown performance mark: ' + name);
  };
  class PerformanceEntry {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
    }
    get name() {
      return state(this).name;
    }
    get entryType() {
      return state(this).entryType;
    }
    get startTime() {
      return state(this).startTime;
    }
    get duration() {
      return state(this).duration;
    }
    toJSON() {
      const s = state(this);
      return {name: s.name, entryType: s.entryType, startTime: s.startTime, duration: s.duration};
    }
  }
  class PerformanceMark extends PerformanceEntry {
    constructor(name, options = {}) {
      super(token);
      if (!arguments.length) throw new TypeError('Mark name required');
      if (options != null && typeof options !== 'object' && typeof options !== 'function')
        throw new TypeError('Mark options must be a dictionary');
      const label = text(name), opts = options == null ? {} : options;
      if (reserved.has(label)) throw syntax('Reserved performance mark name');
      const start = opts.startTime;
      states.set(this, {
        name: label,
        entryType: 'mark',
        startTime: start === undefined ? __webcliNow() : timestamp(start),
        duration: 0,
        detail: detail(opts.detail)
      });
    }
    get detail() {
      return state(this).detail;
    }
    toJSON() {
      return {...super.toJSON(), detail: this.detail};
    }
  }
  class PerformanceMeasure extends PerformanceEntry {
    constructor(key) {
      super(key);
    }
    get detail() {
      return state(this).detail;
    }
    toJSON() {
      return {...super.toJSON(), detail: this.detail};
    }
  }
  class Performance extends EventTarget {
    constructor(key) {
      super();
      if (key !== token) throw new TypeError('Illegal constructor');
    }
    now() {
      check(this);
      return __webcliNow();
    }
    get timeOrigin() {
      check(this);
      return origin;
    }
    get timing() {
      check(this);
      return __legacyPerformanceTiming.instance;
    }
    toJSON() {
      check(this);
      return {timeOrigin: origin};
    }
    getEntries() {
      check(this);
      return entries.slice().sort((a, b) => a.startTime - b.startTime);
    }
    getEntriesByType(type) {
      check(this);
      if (!arguments.length) throw new TypeError('Entry type required');
      const name = text(type);
      return this.getEntries().filter(e => e.entryType === name);
    }
    getEntriesByName(name, type = undefined) {
      check(this);
      if (!arguments.length) throw new TypeError('Entry name required');
      const label = text(name), kind = type === undefined ? undefined : text(type);
      return this.getEntries().filter(
          e => e.name === label && (kind === undefined || e.entryType === kind));
    }
    mark(name, options = {}) {
      check(this);
      if (!arguments.length) throw new TypeError('Mark name required');
      const entry = new PerformanceMark(name, options);
      entries.push(entry);
      globalThis.__webcliQueuePerformanceEntry?.(entry);
      return entry;
    }
    measure(name, startOrOptions = undefined, endMark = undefined) {
      check(this);
      if (!arguments.length) throw new TypeError('Measure name required');
      const label = text(name);
      let start = 0, end = __webcliNow(), metadata = null;
      if (startOrOptions == null || typeof startOrOptions === 'object' ||
          typeof startOrOptions === 'function') {
        const {start: s, end: e, duration: d, detail: info} = startOrOptions || {};
        const populated =
            s !== undefined || e !== undefined || d !== undefined || info !== undefined;
        if (populated &&
            (endMark !== undefined || s !== undefined && e !== undefined && d !== undefined ||
             s === undefined && e === undefined))
          throw new TypeError('Invalid measure boundaries');
        if (endMark !== undefined)
          end = boundary(text(endMark));
        else if (e !== undefined)
          end = boundary(e);
        else if (s !== undefined && d !== undefined)
          end = boundary(s) + timestamp(d);
        if (s !== undefined)
          start = boundary(s);
        else if (e !== undefined && d !== undefined)
          start = end - timestamp(d);
        metadata = detail(info);
      } else {
        if (startOrOptions !== undefined) start = boundary(text(startOrOptions));
        if (endMark !== undefined) end = boundary(text(endMark));
      }
      const entry = new PerformanceMeasure(token);
      states.set(entry, {
        name: label,
        entryType: 'measure',
        startTime: start,
        duration: end - start,
        detail: metadata
      });
      entries.push(entry);
      globalThis.__webcliQueuePerformanceEntry?.(entry);
      return entry;
    }
    clearMarks(name = undefined) {
      clear(this, 'mark', name);
    }
    clearMeasures(name = undefined) {
      clear(this, 'measure', name);
    }
  }
  const instance = new Performance(token);
  function check(value) {
    if (value !== instance) throw new TypeError('Incompatible performance receiver');
  }
  function clear(receiver, type, name) {
    check(receiver);
    const label = name === undefined ? undefined : text(name);
    entries = entries.filter(e => e.entryType !== type || label !== undefined && e.name !== label);
  }
  for (const Type of [Performance, PerformanceEntry, PerformanceMark, PerformanceMeasure]) {
    Object.defineProperty(
        Type.prototype, Symbol.toStringTag, {value: Type.name, configurable: true});
    Object.defineProperty(globalThis, Type.name, {value: Type, writable: true, configurable: true});
  }
  Object.defineProperty(globalThis, 'performance', {value: instance, configurable: true});
})();
