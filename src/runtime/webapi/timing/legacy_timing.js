// Navigation Timing Level 1 compatibility, matching Lightpanda's unmeasured
// fields. Zero is an unavailable timestamp, not a measured load duration.
const __legacyPerformanceTiming = (() => {
  const token = {};
  const fields = Object.freeze(
      ('navigationStart unloadEventStart unloadEventEnd redirectStart ' +
       'redirectEnd fetchStart domainLookupStart domainLookupEnd connectStart connectEnd ' +
       'secureConnectionStart requestStart responseStart responseEnd domLoading domInteractive ' +
       'domContentLoadedEventStart domContentLoadedEventEnd domComplete loadEventStart loadEventEnd')
          .split(' '));
  class PerformanceTiming {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
    }
    toJSON() {
      check(this);
      return Object.fromEntries(fields.map(field => [field, 0]));
    }
  }
  const instance = new PerformanceTiming(token);
  function check(value) {
    if (value !== instance) throw new TypeError('Incompatible PerformanceTiming receiver');
  }
  for (const field of fields)
    Object.defineProperty(PerformanceTiming.prototype, field, {
      enumerable: true,
      configurable: true,
      get() {
        check(this);
        return 0;
      }
    });
  Object.defineProperty(
      PerformanceTiming.prototype, Symbol.toStringTag,
      {value: 'PerformanceTiming', configurable: true});
  Object.defineProperty(
      globalThis, 'PerformanceTiming',
      {value: PerformanceTiming, writable: true, configurable: true});
  return Object.freeze({instance, fields});
})();
