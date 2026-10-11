(() => {
  const {readable, readers, controllers, token, deferred, get, strategy, chunkSize, callback} =
      __streams;
  const stateOf = stream => get(readable, stream);
  const close = state => {
    state.status = 'closed';
    for (const request of state.requests.splice(0)) request.resolve({value: undefined, done: true});
    if (state.reader) readers.get(state.reader).closed.resolve();
  };
  const error = (state, reason) => {
    if (state.status !== 'readable') return;
    state.status = 'errored';
    state.error = reason;
    state.queue.length = 0;
    state.total = 0;
    for (const request of state.requests.splice(0)) request.reject(reason);
    if (state.reader) readers.get(state.reader).closed.reject(reason);
  };
  const pull = state => {
    if (!state.started || state.status !== 'readable' || state.closing ||
        !(state.requests.length || state.highWaterMark > state.total))
      return;
    if (state.pulling) {
      state.pullAgain = true;
      return;
    }
    state.pulling = true;
    __nativeResolve()
        .then(() => state.pull(state.controller))
        .then(
            () => {
              state.pulling = false;
              if (state.pullAgain) {
                state.pullAgain = false;
                pull(state);
              }
            },
            reason => {
              state.pulling = false;
              error(state, reason);
            });
  };
  const cancel = (state, reason) => {
    state.disturbed = true;
    if (state.status === 'closed') return __nativeResolve();
    if (state.status === 'errored') return __nativeReject(state.error);
    state.queue.length = 0;
    state.total = 0;
    close(state);
    return __nativeResolve().then(() => state.cancel(reason)).then(() => undefined);
  };
  globalThis.ReadableStreamDefaultController = class ReadableStreamDefaultController {
    constructor(key, state) {
      if (key !== token) throw new TypeError('Illegal constructor');
      controllers.set(this, state);
    }
    get desiredSize() {
      const state = get(controllers, this);
      return state.status === 'errored' ? null :
          state.status === 'closed'     ? 0 :
                                          state.highWaterMark - state.total;
    }
    enqueue(chunk) {
      const state = get(controllers, this);
      if (state.status !== 'readable' || state.closing)
        throw new TypeError('Stream cannot enqueue');
      if (state.requests.length)
        state.requests.shift().resolve({value: chunk, done: false});
      else {
        let size;
        try {
          size = chunkSize(state.size, chunk);
          if (state.queue.length >= 10000)
            throw new DOMException('Stream queue limit exceeded', 'QuotaExceededError');
        } catch (reason) {
          error(state, reason);
          throw reason;
        }
        state.queue.push({chunk, size});
        state.total += size;
      }
      pull(state);
    }
    close() {
      const state = get(controllers, this);
      if (state.status !== 'readable' || state.closing) throw new TypeError('Stream cannot close');
      state.closing = true;
      if (!state.queue.length) close(state);
    }
    error(reason) {
      error(get(controllers, this), reason);
    }
    get[Symbol.toStringTag]() {
      return 'ReadableStreamDefaultController';
    }
  };
  globalThis.ReadableStreamDefaultReader = class ReadableStreamDefaultReader {
    constructor(stream) {
      const state = stateOf(stream);
      if (state.reader) throw new TypeError('Stream is locked');
      const closed = deferred();
      readers.set(this, {state, closed});
      state.reader = this;
      if (state.status === 'closed')
        closed.resolve();
      else if (state.status === 'errored')
        closed.reject(state.error);
    }
    get closed() {
      return get(readers, this).closed.promise;
    }
    read() {
      const record = get(readers, this), state = record.state;
      if (!state) return __nativeReject(new TypeError('Reader has been released'));
      state.disturbed = true;
      if (state.status === 'closed') return __nativeResolve({value: undefined, done: true});
      if (state.status === 'errored') return __nativeReject(state.error);
      if (state.queue.length) {
        const {chunk, size} = state.queue.shift();
        state.total -= size;
        if (!state.queue.length && state.closing)
          close(state);
        else
          pull(state);
        return __nativeResolve({value: chunk, done: false});
      }
      const request = deferred();
      state.requests.push(request);
      pull(state);
      return request.promise;
    }
    cancel(reason) {
      const state = get(readers, this).state;
      return state ? cancel(state, reason) :
                     __nativeReject(new TypeError('Reader has been released'));
    }
    releaseLock() {
      const record = get(readers, this), state = record.state;
      if (!state) return;
      const reason = new TypeError('Reader has been released');
      for (const request of state.requests.splice(0)) request.reject(reason);
      if (state.status !== 'readable') record.closed = deferred();
      record.closed.reject(reason);
      state.reader = null;
      record.state = null;
    }
    get[Symbol.toStringTag]() {
      return 'ReadableStreamDefaultReader';
    }
  };
  globalThis.ReadableStream = class ReadableStream {
    constructor(source = {}, queueing = {}) {
      if (source === null || queueing === null) throw new TypeError('Stream dictionaries required');
      if (source.type !== undefined)
        throw new RangeError('Byte stream controllers are not supported');
      const sizing = strategy(queueing, 1);
      const start = callback(source, 'start'), state = {
        ...sizing,
        status: 'readable',
        error: undefined,
        queue: [],
        total: 0,
        requests: [],
        reader: null,
        started: false,
        pulling: false,
        pullAgain: false,
        closing: false,
        disturbed: false,
        pull: callback(source, 'pull'),
        cancel: callback(source, 'cancel')
      };
      state.controller = new ReadableStreamDefaultController(token, state);
      readable.set(this, state);
      const result = start(state.controller);
      __nativeResolve(result).then(() => {
        state.started = true;
        pull(state);
      }, reason => error(state, reason));
    }
    get locked() {
      return stateOf(this).reader !== null;
    }
    getReader(options = {}) {
      if (options.mode !== undefined) throw new RangeError('BYOB readers are not supported');
      return new ReadableStreamDefaultReader(this);
    }
    cancel(reason) {
      const state = stateOf(this);
      return state.reader ? __nativeReject(new TypeError('Stream is locked')) :
                            cancel(state, reason);
    }
    tee() {
      const reader = this.getReader(), cancelled = [false, false], reasons = [], done = deferred();
      let pulling = false, again = false, ended = false;
      const controls = [];
      const finish = () => {
        if (!ended) {
          ended = true;
          reader.releaseLock();
          done.resolve();
        }
      };
      reader.closed.catch(reason => {
        if (ended) return;
        for (let i = 0; i < 2; i++)
          if (!cancelled[i]) controls[i].error(reason);
        finish();
      });
      const read = () => {
        if (ended) return;
        if (pulling) {
          again = true;
          return;
        }
        pulling = true;
        return reader.read()
            .then(
                result => {
                  if (result.done) {
                    for (let i = 0; i < 2; i++)
                      if (!cancelled[i]) controls[i].close();
                    finish();
                  } else
                    for (let i = 0; i < 2; i++)
                      if (!cancelled[i]) controls[i].enqueue(result.value);
                },
                reason => {
                  for (let i = 0; i < 2; i++)
                    if (!cancelled[i]) controls[i].error(reason);
                  finish();
                })
            .then(() => {
              pulling = false;
              if (again && !ended) {
                again = false;
                return read();
              }
            });
      };
      return [0, 1].map(index => new ReadableStream({
                          start(controller) {
                            controls[index] = controller;
                          },
                          pull: read,
                          cancel(reason) {
                            cancelled[index] = true;
                            reasons[index] = reason;
                            if (cancelled[0] && cancelled[1] && !ended)
                              return reader.cancel(reasons).finally(finish);
                            return done.promise;
                          }
                        }));
    }
    values(options = {}) {
      const reader = this.getReader();
      let ended = false;
      return {
        async next() {
          if (ended) return {done: true, value: undefined};
          try {
            const result = await reader.read();
            if (result.done) {
              ended = true;
              reader.releaseLock();
            }
            return result;
          } catch (error) {
            ended = true;
            reader.releaseLock();
            throw error;
          }
        },
        async return (value) {
          if (!ended) {
            ended = true;
            try {
              if (!options.preventCancel) await reader.cancel(value);
            } finally {
              reader.releaseLock();
            }
          }
          return {done: true, value};
        },
        [Symbol.asyncIterator]() {
          return this;
        }
      };
    }
    [Symbol.asyncIterator](options = {}) {
      return this.values(options);
    }
    get[Symbol.toStringTag]() {
      return 'ReadableStream';
    }
  };
})();
