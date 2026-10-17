(() => {
  const {writable, writers, token, deferred, get, strategy, chunkSize, callback} = __streams;
  const controllers = new WeakMap();
  const stateOf = stream => get(writable, stream);
  const error = (state, reason) => {
    if (state.status !== 'writable') return;
    state.status = 'errored';
    state.error = reason;
    if (state.writer) {
      const record = writers.get(state.writer);
      record.ready.reject(reason);
      record.ready = deferred();
      record.ready.reject(reason);
      record.closed.reject(reason);
    }
  };
  const updateReady = state => {
    if (!state.writer || state.status !== 'writable') return;
    const record = writers.get(state.writer);
    if (state.highWaterMark - state.total <= 0 && !state.closing) {
      if (!state.pressured) record.ready = deferred();
      state.pressured = true;
    } else {
      state.pressured = false;
      record.ready.resolve();
    }
  };
  const enqueue = (state, operation, size = 0) => {
    state.total += size;
    updateReady(state);
    const result = state.tail.then(() => {
      if (state.status === 'errored') throw state.error;
      return operation();
    });
    state.tail = result.then(() => {
      state.total -= size;
      updateReady(state);
    }, reason => {
      state.total -= size;
      error(state, reason);
    });
    return result;
  };
  const abort = (state, reason) => {
    if (state.status === 'closed') return __nativeResolve();
    if (state.status === 'errored') return __nativeReject(state.error);
    error(state, reason);
    state.abortController.abort(reason);
    return state.tail.then(() => state.abort(reason)).then(() => undefined);
  };
  const close = state => {
    if (state.status !== 'writable' || state.closing)
      return __nativeReject(state.error ?? new TypeError('Stream cannot close'));
    state.closing = true;
    updateReady(state);
    return enqueue(state, () => __nativeResolve(state.close()).then(() => {
      if (state.status === 'errored') throw state.error;
      state.status = 'closed';
      if (state.writer) writers.get(state.writer).closed.resolve();
    }));
  };
  __streams.writableError = error;
  globalThis.WritableStreamDefaultController = class WritableStreamDefaultController {
    constructor(key, state) {
      if (key !== token) throw new TypeError('Illegal constructor');
      controllers.set(this, state);
    }
    get signal() { return get(controllers, this).abortController.signal; }
    error(reason) { error(get(controllers, this), reason); }
    get [Symbol.toStringTag]() { return 'WritableStreamDefaultController'; }
  };
  globalThis.WritableStreamDefaultWriter = class WritableStreamDefaultWriter {
    constructor(stream) {
      const state = stateOf(stream);
      if (state.writer) throw new TypeError('Stream is locked');
      const record = {state, ready: deferred(), closed: deferred()};
      writers.set(this, record);
      state.writer = this;
      state.pressured = false;
      if (state.status === 'errored') {
        record.ready.reject(state.error);
        record.closed.reject(state.error);
      } else {
        updateReady(state);
        if (state.status === 'closed') {
          record.ready.resolve();
          record.closed.resolve();
        }
      }
    }
    get ready() { return get(writers, this).ready.promise; }
    get closed() { return get(writers, this).closed.promise; }
    get desiredSize() {
      const state = get(writers, this).state;
      if (!state) throw new TypeError('Writer has been released');
      return state.status === 'errored' ? null : state.status === 'closed' ? 0 : state.highWaterMark - state.total;
    }
    write(chunk) {
      const state = get(writers, this).state;
      if (!state || state.status !== 'writable' || state.closing)
        return __nativeReject(state?.error ?? new TypeError('Stream cannot write'));
      let size;
      try { size = chunkSize(state.size, chunk); }
      catch (reason) { error(state, reason); return __nativeReject(reason); }
      return enqueue(state, () => state.write(chunk, state.controller), size);
    }
    close() {
      const state = get(writers, this).state;
      return state ? close(state) : __nativeReject(new TypeError('Writer has been released'));
    }
    abort(reason) {
      const state = get(writers, this).state;
      return state ? abort(state, reason) : __nativeReject(new TypeError('Writer has been released'));
    }
    releaseLock() {
      const record = get(writers, this), state = record.state;
      if (!state) return;
      const reason = new TypeError('Writer has been released');
      record.ready.reject(reason);
      record.closed.reject(reason);
      record.ready = deferred(); record.ready.reject(reason);
      record.closed = deferred(); record.closed.reject(reason);
      state.writer = null;
      record.state = null;
    }
    get [Symbol.toStringTag]() { return 'WritableStreamDefaultWriter'; }
  };
  globalThis.WritableStream = class WritableStream {
    constructor(sink = {}, queueing = {}) {
      if (sink === null || queueing === null) throw new TypeError('Stream dictionaries required');
      if (sink.type !== undefined) throw new RangeError('Unsupported sink type');
      const state = {...strategy(queueing, 1), status: 'writable', error: undefined,
        total: 0, writer: null, pressured: false, closing: false,
        write: callback(sink, 'write'), close: callback(sink, 'close'), abort: callback(sink, 'abort'),
        abortController: new AbortController()};
      state.controller = new WritableStreamDefaultController(token, state);
      writable.set(this, state);
      state.tail = __nativeResolve(callback(sink, 'start')(state.controller)).catch(reason => error(state, reason));
    }
    get locked() { return stateOf(this).writer !== null; }
    getWriter() { return new WritableStreamDefaultWriter(this); }
    abort(reason) {
      const state = stateOf(this);
      return state.writer ? __nativeReject(new TypeError('Stream is locked')) : abort(state, reason);
    }
    close() {
      const state = stateOf(this);
      return state.writer ? __nativeReject(new TypeError('Stream is locked')) : close(state);
    }
    get [Symbol.toStringTag]() { return 'WritableStream'; }
  };
})();
