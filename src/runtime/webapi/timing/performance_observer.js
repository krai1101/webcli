// Observe real User Timing entries; unsupported timing types produce no entries.
{
  const states = new WeakMap(), lists = new WeakMap(), active = new Set();
  const supported = Object.freeze(['mark', 'measure']);
  const schedule = setTimeout, cancel = clearTimeout;
  let task = null;
  const get = observer => {
    const state = states.get(observer);
    if (!state) throw new TypeError('Incompatible PerformanceObserver receiver');
    return state;
  };
  const text = value => {
    if (typeof value === 'symbol') throw new TypeError('Cannot convert Symbol to string');
    return String(value);
  };
  const entries = list => {
    const result = lists.get(list);
    if (!result) throw new TypeError('Incompatible performance entry list');
    return result.slice().sort((a, b) => a.startTime - b.startTime);
  };
  class PerformanceObserverEntryList {
    constructor() { throw new TypeError('Illegal constructor'); }
    getEntries() { return entries(this); }
    getEntriesByType(type) {
      if (!arguments.length) throw new TypeError('Entry type required');
      const name = text(type);
      return entries(this).filter(entry => entry.entryType === name);
    }
    getEntriesByName(name, type) {
      if (!arguments.length) throw new TypeError('Entry name required');
      const label = text(name), kind = type === undefined ? undefined : text(type);
      return entries(this).filter(entry => entry.name === label &&
          (kind === undefined || entry.entryType === kind));
    }
  }
  const deliver = () => {
    task = null;
    for (const observer of [...active]) {
      const state = get(observer);
      if (!state.records.length) continue;
      const list = Object.create(PerformanceObserverEntryList.prototype);
      lists.set(list, observer.takeRecords());
      try { state.callback.call(observer, list, observer, {droppedEntriesCount: 0}); }
      catch (error) { __webcliReportError('PerformanceObserver: ' + (error?.stack || error)); }
    }
  };
  const queue = () => { if (task === null) task = schedule(deliver, 0); };
  class PerformanceObserver {
    constructor(callback) {
      if (typeof callback !== 'function') throw new TypeError('Observer callback required');
      states.set(this, {callback, mode: null, types: new Set(), records: []});
    }
    static get supportedEntryTypes() { return supported; }
    observe(options = {}) {
      const state = get(this);
      if (options != null && typeof options !== 'object' && typeof options !== 'function')
        throw new TypeError('Observer options must be a dictionary');
      const opts = options || {}, multiple = opts.entryTypes, single = opts.type;
      if ((multiple === undefined) === (single === undefined))
        throw new TypeError('Specify either type or entryTypes');
      const mode = single === undefined ? 'multiple' : 'single';
      if (state.mode && state.mode !== mode)
        throw new DOMException('Observer mode cannot change', 'InvalidModificationError');
      if (mode === 'multiple' && (multiple == null ||
          typeof multiple !== 'object' && typeof multiple !== 'function' ||
          typeof multiple[Symbol.iterator] !== 'function'))
        throw new TypeError('entryTypes must be a sequence');
      const types = mode === 'single' ? [text(single)] : Array.from(multiple, text);
      state.mode = mode;
      if (mode === 'multiple') state.types.clear();
      for (const type of types) if (supported.includes(type)) state.types.add(type);
      if (!state.types.size) return;
      active.add(this);
      if (mode === 'single' && opts.buffered && supported.includes(types[0])) {
        state.records.push(...performance.getEntriesByType(types[0]));
        if (state.records.length) queue();
      }
    }
    takeRecords() {
      const state = get(this), result = state.records;
      state.records = [];
      return result;
    }
    disconnect() {
      const state = get(this);
      state.types.clear();
      state.records = [];
      active.delete(this);
      if (!active.size && task !== null) { cancel(task); task = null; }
    }
  }
  globalThis.__webcliQueuePerformanceEntry = entry => {
    for (const observer of active) {
      const state = get(observer);
      if (state.types.has(entry.entryType)) { state.records.push(entry); queue(); }
    }
  };
  Object.assign(globalThis, {PerformanceObserver, PerformanceObserverEntryList});
}
