// Buffered BodyInit ownership shared by requests, responses and Blob readers.
const __bufferedBodies = new WeakMap();

function __binaryBytes(value) {
  if (value instanceof ArrayBuffer) return new Uint8Array(value).slice();
  if (ArrayBuffer.isView(value))
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
        .slice();
  if (typeof __blobBytes === 'function' && value instanceof Blob)
    return __blobBytes(value).slice();
  return new TextEncoder().encode(String(value));
}

function __joinBytes(chunks, maximum = __webcliLimits.bufferedBodyBytes) {
  let size = 0;
  for (const chunk of chunks) {
    size += chunk.byteLength;
    if (size > maximum) throw new RangeError('Byte body exceeds limit');
  }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return bytes;
}

function __bodyInit(value) {
  if (value instanceof ReadableStream) {
    if (value.locked || __streams.readable.get(value).disturbed)
      throw new TypeError('Body stream is unusable');
    return {bytes: null, stream: value, type: ''};
  }
  if (value == null) return {bytes: null, type: ''};
  if (typeof FormData !== 'undefined' && value instanceof FormData)
    return __encodeFormData(value);
  if (value instanceof URLSearchParams)
    return {
      bytes: __binaryBytes(value.toString()),
      type: 'application/x-www-form-urlencoded;charset=UTF-8'
    };
  if (typeof Blob !== 'undefined' && value instanceof Blob)
    return {bytes: __binaryBytes(value), type: value.type};
  const binary = value instanceof ArrayBuffer || ArrayBuffer.isView(value);
  return {
    bytes: __binaryBytes(value),
    type: binary ? '' : 'text/plain;charset=UTF-8'
  };
}

function __installBody(owner, value, headers) {
  const body = __bodyInit(value);
  if (body.bytes && body.bytes.length > __webcliLimits.bufferedBodyBytes)
    throw new RangeError('Body exceeds 8 MiB');
  if (body.type && !headers.has('content-type'))
    headers.set('content-type', body.type);
  __bufferedBodies.set(
      owner,
      {bytes: body.bytes, stream: body.stream ?? null, used: false, headers});
}
class BufferedBody {
  get bodyUsed() {
    const state = __bufferedBodies.get(this);
    return state.used ||
        Boolean(state.stream && __streams.readable.get(state.stream).disturbed);
  }
  get body() {
    const state = __bufferedBodies.get(this);
    if (!state.stream && state.bytes !== null) {
      const bytes = state.bytes, consumed = state.used;
      let offset = 0;
      state.stream = new ReadableStream(
          {
            pull(controller) {
              if (consumed) {
                controller.close();
                return;
              }
              if (offset < bytes.length) {
                const end = Math.min(offset + 65536, bytes.length);
                controller.enqueue(bytes.slice(offset, end));
                offset = end;
              }
              if (offset === bytes.length) controller.close();
            }
          },
          {highWaterMark: 0});
      if (state.used) __streams.readable.get(state.stream).disturbed = true;
    }
    return state.stream;
  }
  #consume(convert) {
    const state = __bufferedBodies.get(this);
    if (this.bodyUsed || state.stream?.locked)
      return __nativeReject(new TypeError('Body already consumed or locked'));
    if (this.body) {
      state.used = true;
      return __readBodyStream(state.stream).then(convert);
    }
    if (state.bytes !== null) state.used = true;
    try {
      return __nativeResolve(convert(state.bytes || new Uint8Array()));
    } catch (error) {
      return __nativeReject(error);
    }
  }
  text() {
    return this.#consume(bytes => new TextDecoder().decode(bytes));
  }
  json() {
    return this.text().then(JSON.parse);
  }
  arrayBuffer() {
    return this.#consume(bytes => bytes.slice().buffer);
  }
  bytes() {
    return this.#consume(bytes => bytes.slice());
  }
  blob() {
    return this.#consume(
        bytes => new Blob([bytes], {
          type: __bufferedBodies.get(this).headers.get('content-type') || ''
        }));
  }
  formData() {
    const type = __bufferedBodies.get(this).headers.get('content-type');
    return this.#consume(bytes => __parseFormData(bytes, type));
  }
}

function __cloneBody(source, target) {
  const body = __bufferedBodies.get(source);
  if (source.bodyUsed || body.stream?.locked)
    throw new TypeError('Body already consumed or locked');
  let stream = null;
  if (body.stream) {
    const branches = body.stream.tee();
    body.stream = branches[0];
    stream = branches[1];
  }
  __bufferedBodies.set(
      target,
      {bytes: body.bytes, stream, used: false, headers: target.headers});
}

async function __readBodyStream(stream, signal) {
  const reader = stream.getReader(), chunks = [];
  let size = 0;
  const unsubscribe = signal ?
      __subscribeAbort(
          signal,
          () => reader.cancel(__abortEngine.reason(signal)).catch(() => {})) :
      () => {};
  try {
    while (true) {
      const {value, done} = await reader.read();
      if (done) break;
      if (!(value instanceof Uint8Array))
        throw new TypeError('Body stream must produce Uint8Array chunks');
      size += value.byteLength;
      if (size > __webcliLimits.bufferedBodyBytes)
        throw new RangeError('Body exceeds 8 MiB');
      chunks.push(value.slice());
    }
    if (signal) __abortEngine.throwIfAborted(signal);
    return __joinBytes(chunks);
  } catch (error) {
    try {
      await reader.cancel(error);
    } catch (_) {
    }
    throw error;
  } finally {
    unsubscribe();
    reader.releaseLock();
  }
}
