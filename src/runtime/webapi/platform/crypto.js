// Secure random values come from libuv's OS random source, never Math.random.
{
  const randomBytes = __webcliRandomBytes;
  const typeName =
      Object
          .getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype), Symbol.toStringTag)
          .get;
  globalThis.crypto = {
    getRandomValues(array) {
      if (!ArrayBuffer.isView(array) ||
          !['Uint8ClampedArray', 'Uint8Array', 'Int8Array', 'Uint16Array', 'Int16Array',
            'Uint32Array', 'Int32Array', 'BigInt64Array', 'BigUint64Array']
               .includes(typeName.call(array)))
        throw new TypeError('expected an integer typed array');
      if (array.byteLength > 65536) {
        const error = new Error('random byte quota exceeded');
        error.name = 'QuotaExceededError';
        throw error;
      }
      const bytes = new Uint8Array(array.buffer, array.byteOffset, array.byteLength);
      bytes.set(new Uint8Array(randomBytes(bytes.length)));
      return array;
    },
    randomUUID() {
      const bytes = this.getRandomValues(new Uint8Array(16));
      bytes[6] = (bytes[6] & 15) | 64;
      bytes[8] = (bytes[8] & 63) | 128;
      const hex = Array.from(bytes, byte => byte.toString(16).padStart(2, '0'));
      return hex.slice(0, 4).join('') + '-' + hex.slice(4, 6).join('') + '-' +
          hex.slice(6, 8).join('') + '-' + hex.slice(8, 10).join('') + '-' + hex.slice(10).join('');
    }
  };
}
