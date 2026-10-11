// Internal SPA history serialization. Getters run once; copies preserve graph
// identity.
const __cloneHistoryState = (() => {
  const errors =
      new Map([Error, EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError].map(
          type => [type.name, type]));
  const arrays = new Map([
    Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array,
    Float32Array, Float64Array, BigInt64Array, BigUint64Array
  ].map(type => [type.name, type]));
  const arrayName =
      Object
          .getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype), Symbol.toStringTag)
          .get;
  const fail = message => {
    throw new DOMException(message, 'DataCloneError');
  };
  return function(value) {
    const seen = new Map();
    let bytes = 0, hasPlatformValues = false;
    const charge = amount => {
      bytes += amount;
      if (bytes > __webcliLimits.bufferedBodyBytes) fail('Clone storage exceeds resource limits');
    };
    const copy = (input, depth = 0) => {
      if (typeof input === 'function' || typeof input === 'symbol') fail('Value cannot be cloned');
      if (input === null || typeof input !== 'object') {
        charge(
            typeof input === 'string'     ? input.length * 2 :
                typeof input === 'bigint' ? BigInt.prototype.toString.call(input).length * 2 :
                                            8);
        return input;
      }
      if (__webcliIsProxy(input)) fail('Proxy objects cannot be cloned');
      if (seen.has(input)) return seen.get(input);
      if (depth > 128 || seen.size >= 10000) fail('Clone graph exceeds resource limits');
      charge(64);
      if (Array.isArray(input)) charge(input.length * 8);
      let output;
      if (input instanceof ArrayBuffer) {
        charge(input.byteLength);
        try {
          output = input.slice(0);
        } catch (_) {
          fail('Detached ArrayBuffer');
        }
      } else if (ArrayBuffer.isView(input)) {
        const buffer = copy(input.buffer, depth + 1);
        output = input instanceof DataView ?
            new DataView(buffer, input.byteOffset, input.byteLength) :
            new (arrays.get(arrayName.call(input)))(buffer, input.byteOffset, input.length);
      } else if (input instanceof Date)
        output = new Date(Date.prototype.getTime.call(input));
      else if (input instanceof RegExp) {
        hasPlatformValues = true;
        output = new RegExp(copy(input.source, depth + 1), input.flags);
      } else if (input instanceof Blob) {
        hasPlatformValues = true;
        charge(input.size);
        output = input instanceof File ?
            new File([input], input.name, {type: input.type, lastModified: input.lastModified}) :
            input.slice(0, input.size, input.type);
      } else if (input instanceof Error) {
        hasPlatformValues = true;
        output = new (errors.get(input.name) || Error)(copy(String(input.message), depth + 1));
        seen.set(input, output);
        if ('cause' in input) output.cause = copy(input.cause, depth + 1);
        if (input.stack !== undefined) output.stack = copy(String(input.stack), depth + 1);
        return output;
      } else if (input instanceof Boolean)
        output = new Boolean(Boolean.prototype.valueOf.call(input));
      else if (input instanceof Number)
        output = new Number(Number.prototype.valueOf.call(input));
      else if (input instanceof String)
        output = new String(copy(String.prototype.valueOf.call(input), depth + 1));
      else if (Object.prototype.toString.call(input) === '[object BigInt]')
        output = Object(BigInt.prototype.valueOf.call(input));
      else if (input instanceof Map)
        output = new Map();
      else if (input instanceof Set)
        output = new Set();
      else if (Array.isArray(input))
        output = new Array(input.length);
      else {
        if (globalThis.DomNode && input instanceof DomNode || input instanceof EventTarget || input instanceof Promise ||
            input instanceof WeakMap || input instanceof WeakSet ||
            typeof SharedArrayBuffer !== 'undefined' && input instanceof SharedArrayBuffer)
          fail('Unsupported message object');
        const tag = Object.prototype.toString.call(input);
        if (tag !== '[object Object]') fail('Unsupported message object');
        output = {};
      }
      seen.set(input, output);
      if (input instanceof Map)
        for (const [key, item] of Map.prototype.entries.call(input))
          output.set(copy(key, depth + 1), copy(item, depth + 1));
      else if (input instanceof Set)
        for (const item of Set.prototype.values.call(input)) output.add(copy(item, depth + 1));
      else if (Array.isArray(input) || Object.prototype.toString.call(input) === '[object Object]')
        for (const key of Object.keys(input)) {
          charge(key.length * 2 + 24);
          Object.defineProperty(output, key, {
            value: copy(input[key], depth + 1),
            enumerable: true,
            writable: true,
            configurable: true
          });
        }
      return output;
    };
    const result = copy(value);
    return {value: result, bytes, hasPlatformValues};
  };
})();
