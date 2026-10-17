(() => {
  const {token, deferred, get, callback} = __streams;
  const controllers = new WeakMap();
  const fail = (state, reason) => {
    state.error = reason;
    state.failed = true;
    state.readController.error(reason);
    if (state.writable) __streams.writableError(__streams.writable.get(state.writable), reason);
    state.gate.resolve();
  };
  globalThis.TransformStreamDefaultController = class TransformStreamDefaultController {
    constructor(key, state) {
      if (key !== token) throw new TypeError('Illegal constructor');
      controllers.set(this, state);
    }
    get desiredSize() { return get(controllers, this).readController.desiredSize; }
    enqueue(chunk) {
      const state = get(controllers, this);
      try {
        state.readController.enqueue(chunk);
        if (state.readController.desiredSize <= 0 && !state.pressured) {
          state.pressured = true;
          state.gate = deferred();
        }
      } catch (reason) { fail(state, reason); throw reason; }
    }
    error(reason) { fail(get(controllers, this), reason); }
    terminate() {
      const state = get(controllers, this);
      state.readController.close();
      state.failed = true;
      state.error = new TypeError('Transform stream terminated');
      __streams.writableError(__streams.writable.get(state.writable), state.error);
      state.gate.resolve();
    }
    get [Symbol.toStringTag]() { return 'TransformStreamDefaultController'; }
  };
  const transforms = new WeakMap();
  globalThis.TransformStream = class TransformStream {
    constructor(transformer = {}, writableStrategy = {}, readableStrategy = {}) {
      if (transformer === null) throw new TypeError('Transformer dictionary required');
      if (transformer.readableType !== undefined || transformer.writableType !== undefined)
        throw new RangeError('Typed transforms are unsupported');
      const state = {gate: deferred(), pressured: true, failed: false, error: undefined};
      const start = deferred();
      const controller = new TransformStreamDefaultController(token, state);
      const transform = transformer.transform === undefined
        ? chunk => controller.enqueue(chunk) : callback(transformer, 'transform');
      const flush = callback(transformer, 'flush');
      const readable = new ReadableStream({
        start(value) { state.readController = value; return start.promise; },
        pull() { state.pressured = false; state.gate.resolve(); },
        cancel(reason) { fail(state, reason); }
      }, {...readableStrategy, highWaterMark: readableStrategy.highWaterMark ?? 0});
      const writable = new WritableStream({
        start() { return start.promise; },
        async write(chunk) {
          if (state.pressured) await state.gate.promise;
          if (state.failed) throw state.error;
          try { await transform(chunk, controller); }
          catch (reason) { fail(state, reason); throw reason; }
          if (state.failed) throw state.error;
        },
        async close() {
          try { await flush(controller); }
          catch (reason) { fail(state, reason); throw reason; }
          if (state.failed) throw state.error;
          state.readController.close();
        },
        abort(reason) { fail(state, reason); }
      }, writableStrategy);
      state.writable = writable;
      // Abort must release a write waiting for readable demand before awaiting
      // the writable queue's completion.
      __streams.writable.get(writable).abortController.signal.addEventListener('abort', () => {
        fail(state, __streams.writable.get(writable).abortController.signal.reason);
      });
      transforms.set(this, {readable, writable});
      try { start.resolve(callback(transformer, 'start')(controller)); }
      catch (reason) { start.reject(reason); throw reason; }
      start.promise.catch(reason => fail(state, reason));
    }
    get readable() { return get(transforms, this).readable; }
    get writable() { return get(transforms, this).writable; }
    get [Symbol.toStringTag]() { return 'TransformStream'; }
  };
})();
