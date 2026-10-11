// WebCrypto subset: secure randomness, digests and AES-GCM.
(() => {
  const random = crypto, digest = __webcliCryptoDigest;
  const token = Symbol('crypto constructor');
  const cryptoInstances = new WeakSet(), subtleInstances = new WeakSet();
  const assertReceiver = (instances, value) => {
    if (!instances.has(value)) throw new TypeError('Illegal invocation');
  };
  globalThis.SubtleCrypto = class SubtleCrypto {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
      subtleInstances.add(this);
    }
    async digest(algorithm, data) {
      assertReceiver(subtleInstances, this);
      const value = typeof algorithm === 'string' ? algorithm : algorithm?.name;
      if (value === undefined) throw new TypeError('Algorithm name required');
      const name = String(value).toUpperCase();
      if (!['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'].includes(name))
        throw new DOMException('Unsupported digest algorithm', 'NotSupportedError');
      let bytes;
      if (data instanceof ArrayBuffer)
        bytes = new Uint8Array(data);
      else if (ArrayBuffer.isView(data))
        bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
      else
        throw new TypeError('BufferSource required');
      if (bytes.byteLength > __webcliLimits.bufferedBodyBytes)
        throw new DOMException('Digest input exceeds byte quota', 'QuotaExceededError');
      return digest(name, bytes.slice().buffer);
    }
    async generateKey(algorithm, extractable, keyUsages) {
      assertReceiver(subtleInstances, this);
      return __aesGcm.generate(algorithm, extractable, keyUsages);
    }
    async importKey(format, data, algorithm, extractable, keyUsages) {
      assertReceiver(subtleInstances, this);
      return __aesGcm.import(format, data, algorithm, extractable, keyUsages);
    }
    async exportKey(format, key) {
      assertReceiver(subtleInstances, this);
      return __aesGcm.export(format, key);
    }
    async encrypt(algorithm, key, data) {
      assertReceiver(subtleInstances, this);
      return __aesGcm.crypt(algorithm, key, data, false);
    }
    async decrypt(algorithm, key, data) {
      assertReceiver(subtleInstances, this);
      return __aesGcm.crypt(algorithm, key, data, true);
    }
    get[Symbol.toStringTag]() {
      return 'SubtleCrypto';
    }
  };
  const subtle = new SubtleCrypto(token);
  globalThis.Crypto = class Crypto {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
      cryptoInstances.add(this);
    }
    get subtle() {
      assertReceiver(cryptoInstances, this);
      return subtle;
    }
    getRandomValues(array) {
      assertReceiver(cryptoInstances, this);
      return random.getRandomValues(array);
    }
    randomUUID() {
      assertReceiver(cryptoInstances, this);
      return random.randomUUID();
    }
    get[Symbol.toStringTag]() {
      return 'Crypto';
    }
  };
  globalThis.crypto = new Crypto(token);
})();
