// Stream bookkeeping is private to a document, on its JavaScript owner thread.
const __streams = (() => {
  const readable = new WeakMap(), readers = new WeakMap(), controllers = new WeakMap();
  const token = Symbol('stream constructor');
  const deferred = () => {
    let resolve, reject;
    const promise = new __nativePromise((yes, no) => {
      resolve = yes;
      reject = no
    });
    // Internal lifecycle promises can reject before a reader/writer is acquired.
    promise.catch(() => {});
    return {promise, resolve, reject};
  };
  const get = (map, object) => {
    const state = map.get(object);
    if (!state) throw new TypeError('Illegal invocation');
    return state;
  };
  const strategy = (value, fallback) => {
    const highWaterMark = Number(value.highWaterMark ?? fallback), size = value.size;
    if (Number.isNaN(highWaterMark) || highWaterMark < 0)
      throw new RangeError('Invalid high water mark');
    if (size !== undefined && typeof size !== 'function')
      throw new TypeError('Size must be callable');
    return {highWaterMark, size: size ?? (() => 1)};
  };
  const chunkSize = (size, chunk) => {
    const result = Number(size(chunk));
    if (!Number.isFinite(result) || result < 0) throw new RangeError('Invalid chunk size');
    return result;
  };
  const callback = (owner, name) => {
    const fn = owner[name];
    if (fn !== undefined && typeof fn !== 'function')
      throw new TypeError(name + ' must be callable');
    return fn ? fn.bind(owner) : () => undefined;
  };
  return {readable, readers, controllers, token, deferred, get, strategy, chunkSize, callback};
})();
