// Original IndexedDB subset. Native SQL is private, origin-scoped and lazy.
const __idb = (() => {
  const native = __webcliIndexedDB, codec = __webcliIndexedDBCodec;
  const state = new WeakMap(), token = {}, schedule = setTimeout;
  const fail = (name, message = name) => {
    throw new DOMException(message, name);
  };
  const sql = (operation, parameters = []) => {
    try {
      return native(operation, parameters);
    } catch (error) {
      const match = String(error).match(
          /(ConstraintError|QuotaExceededError|AbortError|UnknownError|SecurityError):/);
      if (match) fail(match[1], String(error));
      throw error;
    }
  };
  const get = object => {
    const value = state.get(object);
    if (!value) throw new TypeError('IndexedDB receiver required');
    return value;
  };
  const names = values => {
    const list = [...values].sort();
    Object.defineProperties(list, {
      contains: {value: name => list.includes(String(name))},
      item: {value: index => list[Number(index)] ?? null}
    });
    return Object.freeze(list);
  };
  const encodeValue = value => {
    const clone = __cloneHistoryState(value);
    return codec('encode', __historyValue.pack(clone.value, clone.hasPlatformValues));
  };
  const decodeValue = bytes => __historyValue.unpack(codec('decode', bytes));
  // The gate is document-owned. Native teardown rolls back the current owner;
  // cancelling document timers also discards every queued continuation.
  const gate = [];
  let owner = null;
  const acquire = (identity, start) => {
    gate.push({identity, start});
    if (!owner) next();
  };
  const next = () => {
    if (owner || !gate.length) return;
    const entry = gate.shift();
    owner = entry.identity;
    schedule(entry.start, 0);
  };
  const release = identity => {
    if (owner !== identity) throw new Error('IndexedDB gate ownership mismatch');
    owner = null;
    next();
  };
  const transfer = (previous, identity) => {
    if (owner !== previous) throw new Error('IndexedDB gate ownership mismatch');
    owner = identity;
  };
  return {
    state,
    token,
    schedule,
    fail,
    sql,
    get,
    names,
    encodeValue,
    decodeValue,
    acquire,
    release,
    transfer,
    generation: __webcliTaskGeneration,
    connections: new Map(),
    closeWatch: new Map()
  };
})();
