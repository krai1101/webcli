// Timer and interval IDs share the native sequence and either clear method.
{
  const schedule = setTimeout,
    cancel = clearTimeout,
    intervals = new Map();
  const delayValue = delay => {
    const value = Number(delay);
    return !Number.isFinite(value) ? 0 : Math.max(0, Math.min(Math.trunc(value), __webcliLimits
      .timerDelayMs));
  };
  globalThis.setTimeout = function(callback, delay = 0, ...args) {
    if (typeof callback !== 'function') throw new TypeError('Timer callback must be a function');
    return schedule(() => callback.apply(globalThis, args), delayValue(delay));
  };
  globalThis.setInterval = function(callback, delay = 0, ...args) {
    if (typeof callback !== 'function') throw new TypeError(
      'Interval callback must be a function');
    delay = delayValue(delay);
    let id;
    const run = () => {
      if (!intervals.has(id)) return;
      try {
        callback.apply(globalThis, args);
      } finally {
        if (intervals.has(id)) intervals.set(id, schedule(run, delay));
      }
    };
    id = schedule(run, delay);
    intervals.set(id, id);
    return id;
  };
  globalThis.clearTimeout = globalThis.clearInterval = function(id) {
    id = Number(id);
    if (intervals.has(id)) {
      cancel(intervals.get(id));
      intervals.delete(id);
    } else cancel(id);
  };
}
