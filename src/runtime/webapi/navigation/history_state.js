// Native entries own serialized snapshots; only the active decoded value is
// cached in this document's heap. Snapshots survive whole-document eviction.
const __historyEngine = (() => {
  const commitPrimitive = __webcliHistoryCommit, traversePrimitive = __webcliHistoryTraverse,
        readPrimitive = __webcliHistoryRead, schedule = setTimeout,
        invalidate = __webcliStyleInvalidate;
  let current = __webcliHistoryInitialState, length = __webcliHistoryLength,
      position = __webcliHistoryOffset, cached, hasCached = false;
  const resetCache = () => {
    cached = undefined;
    hasCached = false;
  };
  const commit = (value, url, replace) => {
    const snapshot = __cloneHistoryState(value);
    let id;
    // Commit the native notification before mutating the archive. A queue or
    // URL rejection leaves the current URL/state and all existing entries intact.
    try {
      id = commitPrimitive(
          url, __historyValue.pack(snapshot.value, snapshot.hasPlatformValues), replace, current);
    } catch (error) {
      if (error instanceof RangeError) throw new DOMException(error.message, 'QuotaExceededError');
      if (error instanceof TypeError) throw new DOMException(error.message, 'DataCloneError');
      throw error;
    }
    current = id;
    resetCache();
    if (!replace) {
      length = Math.min(100, position + 2);
      position = length - 1;
    }
    __webcliCurrentURL = url;
    invalidate();
  };
  return Object.freeze({
    commit,
    get length() {
      return length;
    },
    get state() {
      if (!hasCached) {
        cached = __historyValue.unpack(readPrimitive(current));
        hasCached = true;
      }
      return cached;
    },
    traverse(delta) {
      // Traversal is a later task; current script and its microtasks finish first.
      schedule(() => traversePrimitive(delta), 0);
    },
    retain(nativeLength, nativePosition, pendingPushes) {
      length = nativeLength;
      position = nativePosition;
      for (let i = 0; i < pendingPushes; ++i) {
        length = Math.min(100, position + 2);
        position = length - 1;
      }
    },
    restore(url, id, nativeLength, nativePosition, dispatchPop) {
      const oldURL = __webcliCurrentURL;
      current = id;
      length = nativeLength;
      position = nativePosition;
      resetCache();
      __webcliCurrentURL = url;
      invalidate();
      if (dispatchPop) window.dispatchEvent(new PopStateEvent('popstate', {state: this.state}));
      if (oldURL.split('#')[0] === url.split('#')[0] && oldURL !== url)
        schedule(
            () => window.dispatchEvent(new HashChangeEvent('hashchange', {oldURL, newURL: url})),
            0);
    },
    notify() {
      window.dispatchEvent(new PopStateEvent('popstate', {state: this.state}));
    }
  });
})();
