// Async XHR uses the same buffered transport, cancellation and body ownership.
// XHR transport is independent of page-defined fetch wrappers and polyfills.
const __xhrFetch = fetch;
globalThis.XMLHttpRequest = class XMLHttpRequest extends EventTarget {
  constructor() {
    super();
    this.readyState = 0;
    this.status = 0;
    this.statusText = '';
    this.responseURL = '';
    this.responseType = '';
    this.timeout = 0;
    this.withCredentials = false;
    this._text = '';
    this._response = null;
    this._headers = new Headers();
    this._sent = false;
    this._generation = 0;
  }
  get responseText() {
    if (!['', 'text'].includes(this.responseType)) throw new DOMException(
      'Response is not text', 'InvalidStateError');
    return this._text;
  }
  get response() {
    return ['', 'text'].includes(this.responseType) ? this._text : this.readyState === 4 ? this
      ._response : null;
  }
  _state(value) {
    this.readyState = value;
    this.dispatchEvent(new Event('readystatechange'));
  }
  open(method, url, async = true) {
    if (!async) throw new DOMException('Synchronous XHR is unsupported', 'NotSupportedError');
    // Validate before changing the previous request's state.
    const request = new Request(url, {
      method
    });
    ++this._generation;
    this._controller?.abort();
    this._clearTimer();
    this._method = request.method;
    this._url = request.url;
    this._headers = new Headers();
    this._sent = false;
    this._text = '';
    this._response = null;
    this._responseHeaders = null;
    this.status = 0;
    this.statusText = '';
    this.responseURL = '';
    this._state(1);
  }
  setRequestHeader(name, value) {
    if (this.readyState !== 1 || this._sent) throw new DOMException('XHR is not open',
      'InvalidStateError');
    this._headers.append(name, value);
  }
  getResponseHeader(name) {
    return this.readyState >= 2 ? this._responseHeaders?.get(name) ?? null : null;
  }
  getAllResponseHeaders() {
    return this.readyState >= 2 && this._responseHeaders ? [...this._responseHeaders].map(([k,
      v]) => k + ': ' + v + '\r\n').join('') : '';
  }
  send(body = null) {
    if (this.readyState !== 1 || this._sent) throw new DOMException('XHR is not open',
      'InvalidStateError');
    if (!['', 'text', 'json', 'arraybuffer', 'blob'].includes(this.responseType))
      throw new DOMException('Unsupported response type', 'NotSupportedError');
    const generation = this._generation,
      current = () => generation === this._generation;
    this._sent = true;
    this._controller = new AbortController();
    const controller = this._controller;
    this._timer = this.timeout > 0 ? setTimeout(() => this._fail('timeout'), this.timeout) :
      null;
    this.dispatchEvent(new ProgressEvent('loadstart'));
    if (!current()) return;
    const responseType = this.responseType;
    const complete = async () => {
      try {
        const response = await __xhrFetch(this._url, {
          method: this._method,
          body: ['GET', 'HEAD'].includes(this._method) ? null : body,
          headers: this._headers,
          signal: controller.signal,
          credentials: this.withCredentials ? 'include' : 'same-origin'
        });
        if (!current()) return;
        this.status = response.status;
        this.statusText = response.statusText;
        this.responseURL = response.url;
        this._responseHeaders = response.headers;
        this._state(2);
        if (!current() || controller.signal.aborted) return;
        const bytes = await response.bytes();
        if (!current() || controller.signal.aborted) return;
        this._state(3);
        if (!current() || controller.signal.aborted) return;
        const text = new TextDecoder().decode(bytes);
        this._text = ['', 'text'].includes(responseType) ? text : '';
        if (responseType === 'json') {
          try {
            this._response = JSON.parse(text);
          } catch (_) {
            this._response = null;
          }
        } else if (responseType === 'arraybuffer') this._response = bytes.buffer;
        else if (responseType === 'blob') this._response = new Blob([bytes], {
          type: response.headers.get('content-type') || ''
        });
        this.dispatchEvent(new ProgressEvent('progress', {
          lengthComputable: true,
          loaded: bytes.length,
          total: bytes.length
        }));
        if (!current()) return;
        this._sent = false;
        this._state(4);
        if (!current()) return;
        this.dispatchEvent(new ProgressEvent('load', {
          lengthComputable: true,
          loaded: bytes.length,
          total: bytes.length
        }));
        if (current()) this.dispatchEvent(new ProgressEvent('loadend'));
      } catch (_) {
        if (current()) this._fail('error');
      } finally {
        if (current()) this._clearTimer();
      }
    };
    complete();
  }
  _clearTimer() {
    if (this._timer != null) clearTimeout(this._timer);
    this._timer = null;
  }
  _fail(kind) {
    if (!this._sent) return;
    const generation = ++this._generation;
    this._clearTimer();
    this._controller.abort();
    this.status = 0;
    this.statusText = '';
    this.responseURL = '';
    this._responseHeaders = null;
    this._text = '';
    this._response = null;
    this._sent = false;
    this._state(4);
    if (generation !== this._generation) return;
    this.dispatchEvent(new ProgressEvent(kind));
    if (generation !== this._generation) return;
    this.dispatchEvent(new ProgressEvent('loadend'));
    if (kind === 'abort' && generation === this._generation) this.readyState = 0;
  }
  abort() {
    if (this._sent) this._fail('abort');
    else {
      ++this._generation;
      this._clearTimer();
      this.status = 0;
      this._text = '';
      this._response = null;
      this.readyState = 0;
    }
  }
};
for (const [name, value] of Object.entries({
    UNSENT: 0,
    OPENED: 1,
    HEADERS_RECEIVED: 2,
    LOADING: 3,
    DONE: 4
  })) {
  Object.defineProperty(XMLHttpRequest, name, {
    value
  });
  Object.defineProperty(XMLHttpRequest.prototype, name, {
    value
  });
}
