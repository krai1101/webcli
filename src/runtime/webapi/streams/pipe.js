const __streamRace = __nativePromise.race.bind(__nativePromise);
const __streamAllSettled = __nativePromise.allSettled.bind(__nativePromise);
ReadableStream.prototype.pipeTo = async function(destination, options = {}) {
  if (!(destination instanceof WritableStream)) throw new TypeError('WritableStream required');
  if (this.locked || destination.locked) throw new TypeError('Stream is locked');
  const reader = this.getReader(), writer = destination.getWriter();
  const abort = __streams.deferred();
  const signal = options.signal;
  const onAbort = () => abort.reject(signal.reason);
  if (signal) {
    if (signal.aborted) onAbort();
    else signal.addEventListener('abort', onAbort, {once: true});
  }
  try {
    for (;;) {
      const result = await __streamRace([reader.read(), writer.closed.then(() => {
        throw new TypeError('Destination closed before source');
      }), abort.promise]);
      if (result.done) {
        if (!options.preventClose) await writer.close();
        return;
      }
      await __streamRace([writer.write(result.value), abort.promise]);
    }
  } catch (reason) {
    await __streamAllSettled([
      options.preventCancel ? undefined : reader.cancel(reason),
      options.preventAbort ? undefined : writer.abort(reason)
    ]);
    throw reason;
  } finally {
    if (signal) signal.removeEventListener('abort', onAbort);
    reader.releaseLock();
    writer.releaseLock();
  }
};
ReadableStream.prototype.pipeThrough = function(transform, options = {}) {
  if (!(transform?.readable instanceof ReadableStream) || !(transform?.writable instanceof WritableStream))
    throw new TypeError('Readable/writable pair required');
  if (this.locked || transform.writable.locked) throw new TypeError('Stream is locked');
  this.pipeTo(transform.writable, options).catch(() => {});
  return transform.readable;
};
