(() => {
  const communicate = __webcliWorker, pack = __historyValue.pack;
  globalThis.WorkerGlobalScope = class WorkerGlobalScope extends EventTarget {
    constructor() { throw new TypeError('Illegal constructor'); }
  };
  globalThis.DedicatedWorkerGlobalScope = class DedicatedWorkerGlobalScope extends WorkerGlobalScope {};
  Object.setPrototypeOf(globalThis, DedicatedWorkerGlobalScope.prototype);
  delete globalThis.Window;
  globalThis.location = Object.freeze(__parseURL(__webcliCurrentURL));
  globalThis.name = __webcliWorkerName;
  globalThis.onmessage = globalThis.onmessageerror = globalThis.onerror = null;
  globalThis.performance = Object.freeze({
    now: __webcliNow,
    timeOrigin: Date.now() - __webcliNow()
  });
  globalThis.postMessage = function(value, options = []) {
    if (!arguments.length) throw new TypeError('Message required');
    const list = Array.isArray(options) ? options : options?.transfer ?? [];
    if (Array.from(list).length)
      throw new DOMException('Worker transfer lists are unsupported', 'NotSupportedError');
    communicate('send', 0, pack(__cloneHistoryState(value).value));
  };
  globalThis.close = () => communicate('close');
  globalThis.importScripts = (...urls) => {
    for (const url of urls) communicate('import', String(url));
  };
  globalThis.__webcliDeliverWorkerMessage = value =>
      dispatchEvent(new MessageEvent('message', {data: __historyValue.unpack(value)}));
})();
