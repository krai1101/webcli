// Signal state, dependency edges and cancellation algorithms are private.
const __abortEngine = (() => {
  const states = new WeakMap(), controllers = new WeakMap(), token = {};
  const dispatch = EventTarget.prototype.dispatchEvent, AbortEvent = Event;
  const WeakReference = WeakRef, AbortException = DOMException;
  const schedule = setTimeout, timerChunk = __webcliLimits.timerDelayMs;
  const timeoutRoots = new Set();
  const stateOf = signal => {
    const state = states.get(signal);
    if (!state) throw new TypeError('AbortSignal required');
    return state;
  };
  const interested = (signal, state) => state.algorithms.size ||
      state.onabort || (__eventListeners(signal).get('abort')?.length ?? 0);
  const updateRetention = signal => {
    const state = states.get(signal);
    if (!state) return;
    const keep = !state.aborted && interested(signal, state);
    for (const edge of state.sources) {
      const root = edge.root.deref();
      if (!root) continue;
      const source = stateOf(root);
      if (keep)
        source.retained.set(edge, signal);
      else
        source.retained.delete(edge);
      updateRetention(root);
    }
    if (state.timeout) {
      if (!state.aborted && (keep || state.retained.size))
        timeoutRoots.add(signal);
      else
        timeoutRoots.delete(signal);
    }
  };
  const removeEdges = edges => {
    for (const edge of edges) {
      const root = edge.root.deref();
      if (!root) continue;
      const state = stateOf(root);
      state.dependents.delete(edge);
      state.retained.delete(edge);
      updateRetention(root);
    }
    edges.clear();
  };
  const collected = new FinalizationRegistry(removeEdges);
  const mark = (signal, reason) => {
    const state = stateOf(signal);
    state.aborted = true;
    state.reason = reason;
    removeEdges(state.sources);
    collected.unregister(signal);
    updateRetention(signal);
  };
  const runSteps = signal => {
    const state = stateOf(signal), actions = [...state.algorithms];
    state.algorithms.clear();
    for (const action of actions) {
      try {
        action();
      } catch (error) {
        __webcliReportError('abort algorithm: ' + String(error));
      }
    }
    dispatch.call(signal, new AbortEvent('abort'));
  };
  const abort = (signal, reason) => {
    const state = stateOf(signal);
    if (state.aborted) return;
    reason = reason === undefined ? new AbortException('Aborted', 'AbortError') :
                                    reason;
    const dependents = [];
    // Mark every flattened dependent before running any algorithms or events.
    // Reentrant handlers therefore observe a single settled reason everywhere.
    mark(signal, reason);
    for (const edge of [...state.dependents]) {
      const dependent = edge.dependent.deref();
      if (dependent && !stateOf(dependent).aborted) {
        mark(dependent, reason);
        dependents.push(dependent);
      }
    }
    state.dependents.clear();
    state.retained.clear();
    runSteps(signal);
    for (const dependent of dependents) runSteps(dependent);
  };
  const subscribe = (signal, action) => {
    const state = stateOf(signal);
    if (state.aborted) {
      action();
      return () => {};
    }
    state.algorithms.add(action);
    updateRetention(signal);
    return () => {
      state.algorithms.delete(action);
      updateRetention(signal);
    };
  };
  const dependent = signals => {
    const result = new AbortSignal(token), state = stateOf(result);
    for (const source of signals) {
      if (stateOf(source).aborted) {
        mark(result, stateOf(source).reason);
        return result;
      }
    }
    state.dependent = true;
    const roots = new Set();
    for (const source of signals) {
      const sourceState = stateOf(source);
      if (!sourceState.dependent)
        roots.add(source);
      else
        for (const edge of sourceState.sources) {
          const root = edge.root.deref();
          if (root) roots.add(root);
        }
    }
    for (const root of roots) {
      const edge = {root: new WeakReference(root), dependent: new WeakReference(result)};
      state.sources.add(edge);
      stateOf(root).dependents.add(edge);
    }
    if (state.sources.size) collected.register(result, state.sources, result);
    return result;
  };
  class AbortSignal extends EventTarget {
    constructor(key) {
      super();
      if (key !== token) throw new TypeError('Illegal AbortSignal constructor');
      states.set(this, {
        aborted: false,
        reason: undefined,
        algorithms: new Set(),
        sources: new Set(),
        dependents: new Set(),
        retained: new Map(),
        dependent: false,
        onabort: null,
        timeout: false
      });
    }
    get aborted() {
      return stateOf(this).aborted;
    }
    get reason() {
      return stateOf(this).reason;
    }
    get onabort() {
      return stateOf(this).onabort;
    }
    set onabort(value) {
      stateOf(this).onabort = typeof value === 'function' ? value : null;
      updateRetention(this);
    }
    throwIfAborted() {
      const state = stateOf(this);
      if (state.aborted) throw state.reason;
    }
    static abort(reason) {
      const signal = new AbortSignal(token);
      abort(signal, reason);
      return signal;
    }
    static any(signals) {
      if (!arguments.length || signals === null ||
          !['object', 'function'].includes(typeof signals))
        throw new TypeError('Iterable signals required');
      const method = signals[Symbol.iterator];
      if (typeof method !== 'function')
        throw new TypeError('Iterable signals required');
      const values = [];
      for (const signal of {[Symbol.iterator]: () => method.call(signals)}) {
        stateOf(signal);
        values.push(signal);
      }
      return dependent(values);
    }
    static timeout(delay) {
      if (!arguments.length) throw new TypeError('Timeout required');
      const value = +delay;
      delay = Math.trunc(value);
      if (!Number.isFinite(value) || delay < 0 ||
          delay > Number.MAX_SAFE_INTEGER)
        throw new TypeError('Timeout outside supported unsigned integer range');
      const signal = new AbortSignal(token);
      stateOf(signal).timeout = true;
      const reference = new WeakReference(signal);
      const next = remaining => {
        const chunk = Math.min(remaining, timerChunk);
        schedule(() => {
          const target = reference.deref();
          if (!target || stateOf(target).aborted) return;
          if (remaining > chunk)
            next(remaining - chunk);
          else
            abort(target, new AbortException('Timed out', 'TimeoutError'));
        }, chunk);
      };
      next(delay);
      return signal;
    }
    get[Symbol.toStringTag]() {
      return 'AbortSignal';
    }
  }
  class AbortController {
    constructor() {
      controllers.set(this, new AbortSignal(token));
    }
    get signal() {
      const signal = controllers.get(this);
      if (!signal) throw new TypeError('AbortController required');
      return signal;
    }
    abort(reason) {
      abort(controllers.get(this), reason);
    }
    get[Symbol.toStringTag]() {
      return 'AbortController';
    }
  }
  Object.assign(globalThis, {AbortSignal, AbortController});
  return Object.freeze({
    subscribe,
    updateRetention,
    aborted: signal => stateOf(signal).aborted,
    reason: signal => stateOf(signal).reason,
    throwIfAborted: signal => {
      const state = stateOf(signal);
      if (state.aborted) throw state.reason;
    },
    follow: signal => dependent(signal === undefined ? [] : [signal])
  });
})();
const __subscribeAbort = __abortEngine.subscribe;
