// Immutable in-memory byte containers. Browser file uploads remain unsupported.
const __blobContents = new WeakMap();
const __blobURLs = new Map();

function __blobURLValue(url) {
  return __blobURLs.get(url);
}

function __blobBytes(blob) {
  const bytes = __blobContents.get(blob);
  if (!bytes) throw new TypeError("Blob required");
  return bytes;
} {
  const contents = __blobContents;
  const urls = __blobURLs;
  const bytes = __binaryBytes;
  const type = value => /^[\x20-\x7e]*$/.test(String(value)) ? String(value).toLowerCase() : '';
  globalThis.Blob = class Blob {
    constructor(parts = [], init = {}) {
      if (parts == null || typeof parts[Symbol.iterator] !== 'function') throw new TypeError(
        'Blob parts must be iterable');
      const chunks = Array.from(parts, bytes);
      const size = chunks.reduce((sum, part) => sum + part.byteLength, 0);
      if (size > __webcliLimits.bufferedBodyBytes) throw new RangeError('Blob exceeds 8 MiB');
      const result = new Uint8Array(size);
      let offset = 0;
      for (const chunk of chunks) {
        result.set(chunk, offset);
        offset += chunk.byteLength;
      }
      contents.set(this, result);
      Object.defineProperties(this, {
        size: {
          value: size,
          enumerable: true
        },
        type: {
          value: type(init.type ?? ''),
          enumerable: true
        }
      });
    }
    slice(start = 0, end = this.size, contentType = '') {
      const integer = value => Number.isNaN(Number(value)) ? 0 : Math.trunc(Number(value));
      const index = value =>
        value < 0 ? Math.max(this.size + value, 0) : Math.min(value, this.size);
      const from = index(integer(start)),
        to = index(integer(end));
      return new Blob([contents.get(this).subarray(from, Math.max(from, to))], {
        type: contentType
      });
    }
    arrayBuffer() {
      return Promise.resolve(contents.get(this).slice().buffer);
    }
    bytes() {
      return Promise.resolve(contents.get(this).slice());
    }
    text() {
      return Promise.resolve(new TextDecoder().decode(contents.get(this)));
    }
    stream() {
      const bytes=contents.get(this);if(!bytes)throw new TypeError('Blob required');let offset=0;
      return new ReadableStream({pull(controller){
        if(offset<bytes.length){const end=Math.min(offset+65536,bytes.length);controller.enqueue(bytes.slice(offset,end));offset=end;}
        if(offset===bytes.length)controller.close();
      }});
    }
    get[Symbol.toStringTag]() {
      return 'Blob';
    }
  };
  URL.createObjectURL = function(blob) {
    if (!(blob instanceof Blob)) throw new TypeError('Blob required');
    const url = 'blob:' + location.origin + '/' + crypto.randomUUID();
    __webcliRegisterBlobURL(url, contents.get(blob).buffer);
    urls.set(url, blob);
    return url;
  };
  URL.revokeObjectURL = function(url) {
    url = String(url);
    __webcliRevokeBlobURL(url);
    urls.delete(url);
  };
  const loadScript = globalThis.__webcliLoadScript;
  if (typeof loadScript === 'function') globalThis.__webcliLoadScript = function(url, node) {
    url = String(url);
    if (!url.startsWith('blob:')) return loadScript(url, node);
    const blob = urls.get(url);
    if (!blob) return Promise.reject(new TypeError('Blob URL is unavailable or revoked'));
    return blob.text().then(body => ({
      body,
      url,
      status: 200
    }));
  };
}

// In-memory FormData metadata; this does not read local files or enable uploads.
globalThis.File = class File extends Blob {
  constructor(parts, name, options = {}) {
    super(parts, options);
    const modified = options.lastModified === undefined ? Date.now() : Number(options.lastModified);
    Object.defineProperties(this, {
      name: {value: String(name).replace(/\//g, ':'), enumerable: true},
      lastModified: {value: Number.isFinite(modified) ? Math.trunc(modified) : 0, enumerable: true}
    });
  }
  get [Symbol.toStringTag]() { return 'File'; }
};
