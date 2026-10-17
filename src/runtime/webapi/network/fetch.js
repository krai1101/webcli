// HTTP bodies expose streams; transport currently buffers each response.
globalThis.Headers = class Headers {
  constructor(input = {}) {
    this.valuesMap = new Map();
    if (input != null && typeof input[Symbol.iterator] === 'function') {
      for (const pair of input) {
        const values = Array.from(pair);
        if (values.length !== 2)
          throw new TypeError('Header pair requires two items');
        this.append(values[0], values[1]);
      }
    } else
      for (const key of Object.keys(input ?? {})) this.append(key, input[key]);
  }
  _name(key) {
    key = String(key).toLowerCase();
    if (!/^[!#$%&'*+.^_`|~0-9a-z-]+$/.test(key))
      throw new TypeError('Invalid header name');
    return key;
  }
  _value(value) {
    value = String(value).replace(/^[\t ]+|[\t ]+$/g, '');
    if (/[\x00\r\n\u0100-\uffff]/.test(value))
      throw new TypeError('Invalid header value');
    return value;
  }
  append(key, value) {
    key = this._name(key);
    value = this._value(value);
    this.valuesMap.set(
        key,
        this.valuesMap.has(key) ? this.valuesMap.get(key) + ', ' + value :
                                  value);
  }
  set(key, value) {
    this.valuesMap.set(this._name(key), this._value(value));
  }
  get(key) {
    return this.valuesMap.get(this._name(key)) ?? null;
  }
  has(key) {
    return this.valuesMap.has(this._name(key));
  }
  delete(key) {
    this.valuesMap.delete(this._name(key));
  }
  * entries() {
    yield*
        [...this.valuesMap].sort(
            (a, b) => a[0] < b[0] ? -1 :
                a[0] > b[0]       ? 1 :
                                    0);
  }
  * keys() {
    for (const [key] of this) yield key;
  }
  * values() {
    for (const [, value] of this) yield value;
  }
  [Symbol.iterator]() {
    return this.entries();
  }
  forEach(callback, self) {
    for (const [key, value] of this) callback.call(self, value, key, this);
  }
};
globalThis.Response = class Response extends BufferedBody {
  constructor(body = null, init = {}) {
    super();
    const status = Number(init.status ?? 200);
    if (!Number.isInteger(status) || status < 200 || status > 599)
      throw new RangeError('Invalid response status');
    if (body != null && [204, 205, 304].includes(status))
      throw new TypeError('Status cannot have a body');
    this.status = status;
    this.statusText = String(init.statusText ?? '');
    if (/[\r\n]/.test(this.statusText))
      throw new TypeError('Invalid status text');
    this.url = String(init.url ?? '');
    this.redirected = !!init.redirected;
    this.type = init.type ?? 'default';
    this.headers = new Headers(init.headers);
    __installBody(this, body, this.headers);
  }
  get ok() {
    return this.status >= 200 && this.status < 300;
  }
  clone() {
    const result =
        this.status === 0 ? Response.error() : new Response(null, this);
    result.type = this.type;
    result.url = this.url;
    result.redirected = this.redirected;
    __cloneBody(this, result);
    return result;
  }
  static error() {
    const result = new Response();
    result.status = 0;
    result.type = 'error';
    return result;
  }
  static redirect(url, status = 302) {
    if (![301, 302, 303, 307, 308].includes(status))
      throw new RangeError('Invalid redirect status');
    return new Response(null, {
      status,
      headers: {location: __parseURL(String(url), __webcliBaseURI()).href}
    });
  }
  static json(value, init = {}) {
    const text = JSON.stringify(value);
    if (text === undefined)
      throw new TypeError('Value is not JSON serializable');
    const headers = new Headers(init.headers);
    if (!headers.has('content-type'))
      headers.set('content-type', 'application/json');
    return new Response(text, {...init, headers});
  }
};
globalThis.Request = class Request extends BufferedBody {
  constructor(input, init = {}) {
    super();
    const source = input instanceof Request ? input : null;
    this.url =
        __parseURL(source ? source.url : String(input), __webcliBaseURI()).href;
    this.method = String(init.method ?? source?.method ?? 'GET').toUpperCase();
    if (!/^[!#$%&'*+.^_`|~0-9A-Z-]+$/.test(this.method) ||
        ['CONNECT', 'TRACE', 'TRACK'].includes(this.method))
      throw new TypeError('Invalid request method');
    this.headers = new Headers(init.headers ?? source?.headers);
    Object.defineProperty(this, 'signal', {
      value: __abortEngine.follow(init.signal ?? source?.signal),
      enumerable: true
    });
    this.credentials = init.credentials ?? source?.credentials ?? 'same-origin';
    this.mode = init.mode ?? source?.mode ?? 'cors';
    this.redirect = init.redirect ?? source?.redirect ?? 'follow';
    let referrer = String(init.referrer ?? source?.referrer ?? 'about:client');
    if (referrer !== '' && referrer !== 'about:client') {
      const parsed = __parseURL(referrer, __webcliBaseURI());
      referrer = parsed.origin === location.origin ? parsed.href : 'about:client';
    }
    const policy = String(init.referrerPolicy ?? source?.referrerPolicy ?? '');
    if (!['', 'no-referrer', 'no-referrer-when-downgrade', 'origin',
          'origin-when-cross-origin', 'same-origin', 'strict-origin',
          'strict-origin-when-cross-origin', 'unsafe-url'].includes(policy))
      throw new TypeError('invalid fetch referrer policy');
    Object.defineProperties(this, {
      referrer: {value: referrer, enumerable: true},
      referrerPolicy: {value: policy, enumerable: true}
    });
    if (!['omit', 'same-origin', 'include'].includes(this.credentials))
      throw new TypeError('invalid fetch credentials mode');
    if (!['cors', 'same-origin', 'no-cors'].includes(this.mode))
      throw new TypeError('invalid fetch mode');
    if (!['follow', 'error', 'manual'].includes(this.redirect))
      throw new TypeError('invalid fetch redirect policy');
    if (source?.bodyUsed) throw new TypeError('Request body already consumed');
    const body = init.body !== undefined ? init.body :
        source                           ? source.body :
                                           null;
    if (body !== null && ['GET', 'HEAD'].includes(this.method))
      throw new TypeError('GET/HEAD cannot have a body');
    __installBody(this, body, this.headers);
    if (source && init.body === undefined && body !== null)
      __bufferedBodies.get(source).used = true;
  }
  clone() {
    const result = new Request(this.url, {
      method: this.method,
      headers: this.headers,
      signal: this.signal,
      credentials: this.credentials,
      mode: this.mode,
      redirect: this.redirect,
      referrer: this.referrer,
      referrerPolicy: this.referrerPolicy
    });
    __cloneBody(this, result);
    return result;
  }
};
globalThis.fetch = function(input, init) {
  return new __nativePromise((resolve, reject) => {
    const begin = async () => {
      let request;
      try {
        request = new Request(input, init);
        __abortEngine.throwIfAborted(request.signal);
      } catch (error) {
        reject(error);
        return;
      }
      const body = __bufferedBodies.get(request);
      if (body.stream) {
        body.used = true;
        body.bytes = await __readBodyStream(body.stream, request.signal);
        __abortEngine.throwIfAborted(request.signal);
      }
      if (body.bytes !== null) body.used = true;
      if (request.url.startsWith('data:')) {
        try { resolve(__dataURLResponse(request)); } catch (error) { reject(error); }
        return;
      }
      if (request.url.startsWith('blob:')) {
        if (request.method !== 'GET') {
          reject(new TypeError('Blob fetch requires GET'));
          return;
        }
        const blob = __blobURLValue(request.url);
        if (!blob) {
          reject(new TypeError('Blob URL unavailable'));
          return;
        }
        resolve(new Response(
            __blobBytes(blob),
            {url: request.url, headers: {'content-type': blob.type}}));
        return;
      }
      let transfer;
      try {
        transfer = __webcliFetch(request.url, {
          method: request.method,
          body: body.bytes?.buffer ?? null,
          headers: Object.fromEntries([...request.headers].filter(
              ([name]) =>
                  !['cookie', 'cookie2', 'host', 'content-length', 'connection', 'referer',
                    'transfer-encoding', 'proxy-authorization']
                       .includes(name))),
          credentials: request.credentials,
          mode: request.mode,
          redirect: request.redirect,
          referrer: request.referrer,
          referrerPolicy: request.referrerPolicy
        });
      } catch (error) {
        reject(error);
        return;
      }
      const abort = () => {
        __webcliCancelFetch(transfer.requestId);
        reject(__abortEngine.reason(request.signal));
      };
      const cleanup = __subscribeAbort(request.signal, abort);
      transfer.then(
          result => {
            cleanup();
            if (__abortEngine.aborted(request.signal)) return;
            try {
              if (result.opaque) {
                const response = Response.error();
                response.type = result.type;
                resolve(response);
                return;
              }
              if (request.redirect === 'manual' &&
                  [301, 302, 303, 307, 308].includes(result.status)) {
                const response = Response.error();
                response.type = 'opaqueredirect';
                resolve(response);
                return;
              }
              const response = new Response(
                  [204, 205, 304].includes(result.status) ||
                          request.method === 'HEAD' ?
                      null :
                      result.bytes,
                  {
                    ...result,
                    headers: Object.fromEntries(
                        Object.entries(result.headers)
                            .filter(
                                ([name]) => name !== 'set-cookie' &&
                                    name !== 'set-cookie2')),
                    redirected: result.url !== request.url,
                    type: 'basic'
                  });
              resolve(response);
            } catch (error) {
              reject(error);
            }
          },
          error => {
            cleanup();
            if (!__abortEngine.aborted(request.signal)) reject(error);
          });
    };
    begin().catch(reject);
  });
};
