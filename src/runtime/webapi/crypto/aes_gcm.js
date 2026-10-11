// AES-GCM only. Key material stays in private state, never on CryptoKey properties.
const __aesGcm = (() => {
  const operation = __webcliCryptoAESGCM;
  const randomBytes = crypto.getRandomValues.bind(crypto);
  const states = new WeakMap(), token = Symbol('CryptoKey');
  const error = (message, name) => new DOMException(message, name);
  const state = key => {
    const result = states.get(key);
    if (!result) throw new TypeError('CryptoKey required');
    return result;
  };
  function bytes(value) {
    let view;
    if (value instanceof ArrayBuffer)
      view = new Uint8Array(value);
    else if (ArrayBuffer.isView(value))
      view = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    else
      throw new TypeError('BufferSource required');
    if (view.byteLength > __webcliLimits.bufferedBodyBytes)
      throw error('AES-GCM input exceeds byte quota', 'OperationError');
    return view.slice();
  }
  function algorithm(value) {
    const name = typeof value === 'string' ? value : value?.name;
    if (name === undefined) throw new TypeError('Algorithm name required');
    if (String(name).toUpperCase() !== 'AES-GCM')
      throw error('Only AES-GCM is supported here', 'NotSupportedError');
  }
  function usages(value) {
    const result = Array.from(value, String);
    if (!result.length || result.some(v => v !== 'encrypt' && v !== 'decrypt'))
      throw error('AES-GCM requires encrypt or decrypt usages', 'SyntaxError');
    return ['encrypt', 'decrypt'].filter(v => result.includes(v));
  }
  class CryptoKey {
    constructor(secret, material, extractable, keyUsages) {
      if (secret !== token) throw new TypeError('Illegal constructor');
      states.set(this, {material, extractable: Boolean(extractable), usages: keyUsages});
    }
    get type() {
      state(this);
      return 'secret';
    }
    get extractable() {
      return state(this).extractable;
    }
    get algorithm() {
      return {name: 'AES-GCM', length: state(this).material.length * 8};
    }
    get usages() {
      return state(this).usages.slice();
    }
    get[Symbol.toStringTag]() {
      return 'CryptoKey';
    }
  }
  globalThis.CryptoKey = CryptoKey;
  const makeKey = (material, extractable, keyUsages) =>
      new CryptoKey(token, material, extractable, keyUsages);
  return {
    generate(params, extractable, keyUsages) {
      algorithm(params);
      const allowed = usages(keyUsages);
      const length = Number(params.length);
      if (![128, 192, 256].includes(length))
        throw error('AES key length must be 128, 192 or 256', 'OperationError');
      return makeKey(randomBytes(new Uint8Array(length / 8)), extractable, allowed);
    },
    import(format, data, params, extractable, keyUsages) {
      if (format !== 'raw') throw error('Only raw AES keys are supported', 'NotSupportedError');
      algorithm(params);
      const allowed = usages(keyUsages), material = bytes(data);
      if (![16, 24, 32].includes(material.length))
        throw error('Invalid AES key length', 'DataError');
      return makeKey(material, extractable, allowed);
    },
    export(format, key) {
      const item = state(key);
      if (!item.extractable) throw error('Key is not extractable', 'InvalidAccessError');
      if (format !== 'raw') throw error('Only raw AES keys are supported', 'NotSupportedError');
      return item.material.slice().buffer;
    },
    crypt(params, key, data, decrypt) {
      algorithm(params);
      const item = state(key);
      if (!item.usages.includes(decrypt ? 'decrypt' : 'encrypt'))
        throw error('Key usage does not permit this operation', 'InvalidAccessError');
      const iv = bytes(params.iv);
      const aad =
          params.additionalData === undefined ? new Uint8Array() : bytes(params.additionalData);
      const input = bytes(data),
            tag = params.tagLength === undefined ? 128 : Number(params.tagLength);
      if (!iv.length || ![32, 64, 96, 104, 112, 120, 128].includes(tag))
        throw error('Invalid AES-GCM IV or tag length', 'OperationError');
      if ([iv, aad, input].some(v => v.length > __webcliLimits.bufferedBodyBytes))
        throw error('AES-GCM input exceeds byte quota', 'OperationError');
      try {
        return operation(item.material.buffer, iv.buffer, aad.buffer, input.buffer, tag, decrypt);
      } catch (cause) {
        throw error(String(cause.message || cause), 'OperationError');
      }
    }
  };
})();
