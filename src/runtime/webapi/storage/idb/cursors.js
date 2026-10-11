{
  const {
    get,
    state,
    token,
    fail,
    sql,
    enqueue,
    encodeKey,
    decodeKey,
    compareBytes,
    range,
    decodeValue,
    inspectStore,
    writeStore
  } = __idb;
  const inspect = cursor => {
    const s = get(cursor);
    if (s.index) __idb.inspectIndex(s.source);
    else inspectStore(s.store);
    if (!s.positioned || s.iterating) fail('InvalidStateError', 'Cursor is not positioned');
    return s;
  };
  const step = (cursor, bounds, skip, after = null, primary = null, inclusive = false) => {
    const s = get(cursor), store = get(s.store), db = get(get(store.transaction).db);
    const row = s.index ? __idb.indexRows(s.source, bounds, s.direction, 1, skip,
                                       after, primary, inclusive)[0] :
        sql((s.direction.startsWith('prev') ? 'cursorPrevious' : 'cursorNext') +
                (s.keyOnly ? 'Key' : ''),
            [db.name, store.name, ...bounds, skip])[0];
    s.iterating = false;
    s.positioned = !!row;
    if (!row) {
      s.encoded = null;
      s.value = undefined;
      return null;
    }
    s.encoded = row[0];
    s.primary = s.index ? row[1] : row[0];
    s.key = decodeKey(row[0]);
    s.value = s.keyOnly ? undefined : decodeValue(row[s.index ? 2 : 1]);
    return cursor;
  };
  const move = (cursor, key, skip = 0) => {
    const s = inspect(cursor), reverse = s.direction.startsWith('prev');
    let encoded = s.encoded;
    if (key !== undefined) {
      encoded = encodeKey(key);
      const order = compareBytes(encoded, s.encoded);
      if (reverse ? order >= 0 : order <= 0)
        fail('DataError', 'Cursor must move in its iteration direction');
    }
    const bounds = [...s.bounds], offset = reverse ? 4 : 0;
    if (!s.index) {
      bounds[offset] = bounds[offset + 1] = bounds[offset + 3] = encoded;
      bounds[offset + 2] = key === undefined ? 1 : 0;
    }
    s.iterating = true;
    const primary = s.index && key === undefined && !s.direction.endsWith('unique') ? s.primary : null;
    enqueue(get(s.store).transaction, s.source,
            () => step(cursor, bounds, skip, encoded, primary, key !== undefined), s.request);
  };
  class IDBCursor {
    constructor(secret, store, bounds, direction, keyOnly, source = store) {
      if (secret !== token) throw new TypeError('Illegal constructor');
      state.set(this, {
        store,
        source,
        index: source !== store,
        bounds,
        direction,
        keyOnly,
        positioned: false,
        iterating: true,
        encoded: null,
        primary: null,
        key: undefined,
        value: undefined,
        request: null
      });
    }
    get source() {
      return get(this).source;
    }
    get direction() {
      return get(this).direction;
    }
    get key() {
      return get(this).key;
    }
    get primaryKey() {
      const s = get(this);
      return s.primary ? decodeKey(s.primary) : undefined;
    }
    get request() {
      return get(this).request;
    }
    continue(key) {
      move(this, key);
    }
    advance(count) {
      const value = Number(count);
      if (!Number.isFinite(value) || Math.trunc(value) < 1 || value > 4294967295)
        throw new TypeError('Advance count must be a positive unsigned integer');
      move(this, undefined, Math.trunc(value) - 1);
    }
    continuePrimaryKey(key, primaryKey) {
      const s = inspect(this);
      if (!s.index || s.direction.endsWith('unique')) fail('InvalidAccessError');
      if (arguments.length < 2) throw new TypeError('Index key and primary key required');
      const encoded = encodeKey(key), primary = encodeKey(primaryKey);
      const order = compareBytes(encoded, s.encoded) || compareBytes(primary, s.primary);
      if (s.direction === 'prev' ? order >= 0 : order <= 0) fail('DataError');
      s.iterating = true;
      enqueue(get(s.store).transaction, s.source,
              () => step(this, s.bounds, 0, encoded, primary, true), s.request);
    }
    delete() {
      const s = inspect(this), {s: store, db} = inspectStore(s.store, true);
      if (s.keyOnly) fail('InvalidStateError', 'Key cursors cannot delete records');
      const bounds = range(decodeKey(s.primary), true);
      return enqueue(store.transaction, this, () => {
        sql('delete', [db.name, store.name, ...bounds]);
      });
    }
    update(value) {
      if (!arguments.length) throw new TypeError('Value required');
      const s = inspect(this), {metadata} = inspectStore(s.store, true);
      if (s.keyOnly) fail('InvalidStateError', 'Key cursors cannot update records');
      return writeStore(
          s.store, value, metadata.keyPath === null ? decodeKey(s.primary) : undefined, true, this,
          s.primary);
    }
  }
  class IDBCursorWithValue extends IDBCursor {
    get value() {
      return get(this).value;
    }
  }
  const openCursor = (store, query, direction, keyOnly, source = store) => {
    const {s} = inspectStore(store), bounds = range(query);
    direction = String(direction);
    if (!['next', 'nextunique', 'prev', 'prevunique'].includes(direction))
      throw new TypeError('Invalid cursor direction');
    const cursor =
        new (keyOnly ? IDBCursor : IDBCursorWithValue)(token, store, bounds, direction, keyOnly, source);
    const request = enqueue(s.transaction, source, () => step(cursor, bounds, 0));
    get(cursor).request = request;
    return request;
  };
  const openIndexCursor = (index, query, direction, keyOnly) => {
    __idb.inspectIndex(index);
    return openCursor(get(index).store, query, direction, keyOnly, index);
  };
  Object.assign(__idb, {openCursor, openIndexCursor});
  Object.assign(globalThis, {IDBCursor, IDBCursorWithValue});
}
