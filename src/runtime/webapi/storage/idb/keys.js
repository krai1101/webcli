// Order-preserving encoding: numbers, dates, UTF-16 strings, bytes, arrays.
// Keys are detached from caller-owned values before a request is queued.
{
  const {fail} = __idb;
  const encodeKey = value => {
    const seen = new Set(), limit = __webcliLimits.bufferedBodyBytes;
    let buffer = new Uint8Array(64), length = 0;
    const output = {
      get length() {
        return length;
      },
      push(...values) {
        if (length + values.length > limit) fail('DataError', 'Key exceeds clone limits');
        if (length + values.length > buffer.length) {
          const grown =
              new Uint8Array(Math.min(limit, Math.max(length + values.length, buffer.length * 2)));
          grown.set(buffer);
          buffer = grown;
        }
        buffer.set(values, length);
        length += values.length;
      }
    };
    const number = value => {
      const bytes = new Uint8Array(8);
      new DataView(bytes.buffer).setFloat64(0, Object.is(value, -0) ? 0 : value);
      if (bytes[0] & 128)
        for (let i = 0; i < 8; ++i) bytes[i] ^= 255;
      else
        bytes[0] ^= 128;
      output.push(...bytes);
    };
    const write = (key, depth = 0) => {
      if (depth > 128) fail('DataError', 'Key nesting exceeds clone limits');
      if (typeof key === 'number' && !Number.isNaN(key)) {
        output.push(1);
        number(key);
      } else if (key instanceof Date && Number.isFinite(key.getTime())) {
        output.push(2);
        number(key.getTime());
      } else if (typeof key === 'string') {
        if (key.length * 3 + output.length + 2 > limit)
          fail('DataError', 'Key exceeds clone limits');
        output.push(3);
        for (let i = 0; i < key.length; ++i) {
          const unit = key.charCodeAt(i);
          output.push(1, unit >> 8, unit & 255);
        }
        output.push(0);
      } else if (key instanceof ArrayBuffer || ArrayBuffer.isView(key)) {
        let bytes;
        try {
          bytes = key instanceof ArrayBuffer ?
              new Uint8Array(key) :
              new Uint8Array(key.buffer, key.byteOffset, key.byteLength);
        } catch (_) {
          fail('DataError', 'Detached binary key');
        }
        if (bytes.length * 2 + output.length + 2 > limit)
          fail('DataError', 'Key exceeds clone limits');
        output.push(4);
        for (const byte of bytes) output.push(1, byte);
        output.push(0);
      } else if (Array.isArray(key)) {
        if (seen.has(key)) fail('DataError', 'Cyclic key');
        seen.add(key);
        output.push(5);
        const count = key.length;
        if (count + output.length + 2 > limit) fail('DataError', 'Key exceeds clone limits');
        for (let i = 0; i < count; ++i) write(key[i], depth + 1);
        output.push(0);
        seen.delete(key);
      } else
        fail('DataError', 'Invalid IndexedDB key');
      if (output.length > __webcliLimits.bufferedBodyBytes)
        fail('DataError', 'Key exceeds clone limits');
    };
    write(value);
    return buffer.buffer.slice(0, length);
  };
  const decodeKey = buffer => {
    const bytes = new Uint8Array(buffer);
    let position = 0;
    const read = () => {
      const type = bytes[position++];
      if (type === 1 || type === 2) {
        const number = bytes.slice(position, position + 8);
        position += 8;
        if (number[0] & 128)
          number[0] ^= 128;
        else
          for (let i = 0; i < 8; ++i) number[i] ^= 255;
        const value = new DataView(number.buffer).getFloat64(0);
        return type === 2 ? new Date(value) : value;
      }
      if (type === 3) {
        let text = '';
        while (bytes[position++]) {
          text += String.fromCharCode(bytes[position] * 256 + bytes[position + 1]);
          position += 2;
        }
        return text;
      }
      if (type === 4) {
        const result = [];
        while (bytes[position++]) result.push(bytes[position++]);
        return new Uint8Array(result).buffer;
      }
      if (type === 5) {
        const result = [];
        while (bytes[position]) result.push(read());
        ++position;
        return result;
      }
      fail('DataError', 'Invalid stored key');
    };
    return read();
  };
  const compareBytes = (left, right) => {
    const a = new Uint8Array(left), b = new Uint8Array(right);
    for (let i = 0; i < Math.min(a.length, b.length); ++i)
      if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
    return Math.sign(a.length - b.length);
  };
  const rangeState = new WeakMap();
  class IDBKeyRange {
    constructor(token, lower, upper, lowerOpen, upperOpen) {
      if (token !== __idb.token) throw new TypeError('Illegal constructor');
      if (lower && upper &&
          (compareBytes(lower, upper) > 0 ||
           (compareBytes(lower, upper) === 0 && (lowerOpen || upperOpen))))
        fail('DataError');
      rangeState.set(this, {lower, upper, lowerOpen: !!lowerOpen, upperOpen: !!upperOpen});
    }
    get lower() {
      const value = rangeState.get(this).lower;
      return value ? decodeKey(value) : undefined;
    }
    get upper() {
      const value = rangeState.get(this).upper;
      return value ? decodeKey(value) : undefined;
    }
    get lowerOpen() {
      return rangeState.get(this).lowerOpen;
    }
    get upperOpen() {
      return rangeState.get(this).upperOpen;
    }
    includes(key) {
      const value = encodeKey(key), range = rangeState.get(this);
      const low = range.lower ? compareBytes(value, range.lower) : 1;
      const high = range.upper ? compareBytes(value, range.upper) : -1;
      return (low > 0 || (low === 0 && !range.lowerOpen)) &&
          (high < 0 || (high === 0 && !range.upperOpen));
    }
    static only(key) {
      const value = encodeKey(key);
      return new IDBKeyRange(__idb.token, value, value, false, false);
    }
    static lowerBound(key, open = false) {
      return new IDBKeyRange(__idb.token, encodeKey(key), null, open, true);
    }
    static upperBound(key, open = false) {
      return new IDBKeyRange(__idb.token, null, encodeKey(key), true, open);
    }
    static bound(lower, upper, lowerOpen = false, upperOpen = false) {
      return new IDBKeyRange(__idb.token, encodeKey(lower), encodeKey(upper), lowerOpen, upperOpen);
    }
  }
  const range = (query, required = false) => {
    if (query === undefined || query === null) {
      if (required) fail('DataError', 'A key or range is required');
      return [null, null, 0, null, null, null, 0, null];
    }
    const r = rangeState.get(query) || rangeState.get(IDBKeyRange.only(query));
    return [
      r.lower, r.lower, Number(r.lowerOpen), r.lower, r.upper, r.upper, Number(r.upperOpen), r.upper
    ];
  };
  globalThis.IDBKeyRange = IDBKeyRange;
  Object.assign(__idb, {encodeKey, decodeKey, compareBytes, range});
}
