// Nonvisual text/byte helpers. Encoding writes whole Unicode scalar values.
(() => {
  const encoders = new WeakSet();
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const toText = value => {
    if (typeof value === 'symbol') throw new TypeError('cannot convert a Symbol to a string');
    return String(value);
  };
  function utf8(source, destination) {
    let read = 0, written = 0;
    const capacity = destination ? destination.length : Infinity;
    while (read < source.length) {
      let point = source.charCodeAt(read), units = 1;
      if (point >= 0xd800 && point <= 0xdbff) {
        const next = source.charCodeAt(read + 1);
        if (next >= 0xdc00 && next <= 0xdfff) {
          point = 0x10000 + ((point - 0xd800) << 10) + next - 0xdc00;
          units = 2;
        } else point = 0xfffd;
      } else if (point >= 0xdc00 && point <= 0xdfff) point = 0xfffd;
      const bytes = point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
      if (written + bytes > capacity) break;
      if (destination) {
        if (bytes === 1) destination[written] = point;
        else {
          destination[written] = (bytes === 2 ? 0xc0 : bytes === 3 ? 0xe0 : 0xf0) |
            (point >> (6 * (bytes - 1)));
          for (let i = 1; i < bytes; ++i)
            destination[written + i] = 0x80 | ((point >> (6 * (bytes - i - 1))) & 0x3f);
        }
      }
      read += units;
      written += bytes;
    }
    return {read, written};
  }
  globalThis.TextEncoder = class TextEncoder {
    constructor() { encoders.add(this); }
    get encoding() {
      if (!encoders.has(this)) throw new TypeError('invalid TextEncoder receiver');
      return 'utf-8';
    }
    encode(input = '') {
      if (!encoders.has(this)) throw new TypeError('invalid TextEncoder receiver');
      const source = toText(input);
      const output = new Uint8Array(utf8(source, null).written);
      utf8(source, output);
      return output;
    }
    encodeInto(input, destination) {
      if (!encoders.has(this)) throw new TypeError('invalid TextEncoder receiver');
      if (arguments.length < 2) throw new TypeError('encodeInto requires two arguments');
      const source = toText(input);
      if (!(destination instanceof Uint8Array)) throw new TypeError('destination must be Uint8Array');
      return utf8(source, destination);
    }
    get [Symbol.toStringTag]() { return 'TextEncoder'; }
  };
  function invalidBase64() {
    const error = new Error('invalid Base64 character data');
    error.name = 'InvalidCharacterError';
    error.code = 5;
    throw error;
  }
  globalThis.btoa = function btoa(input) {
    if (!arguments.length) throw new TypeError('btoa requires an argument');
    const source = toText(input), chunks = [];
    let output = '';
    for (let i = 0; i < source.length; i += 3) {
      const a = source.charCodeAt(i), b = source.charCodeAt(i + 1), c = source.charCodeAt(i + 2);
      if (a > 255 || b > 255 || c > 255) invalidBase64();
      output += alphabet[a >> 2] + alphabet[((a & 3) << 4) | (b >> 4)] +
        (i + 1 < source.length ? alphabet[((b & 15) << 2) | (c >> 6)] : '=') +
        (i + 2 < source.length ? alphabet[c & 63] : '=');
      if (output.length >= 4096) { chunks.push(output); output = ''; }
    }
    chunks.push(output);
    return chunks.join('');
  };
  globalThis.atob = function atob(input) {
    if (!arguments.length) throw new TypeError('atob requires an argument');
    let source = toText(input).replace(/[\t\n\f\r ]/g, '');
    if (source.length % 4 === 0) source = source.replace(/={1,2}$/, '');
    if (source.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(source)) invalidBase64();
    let buffer = 0, bits = 0, output = '';
    const chunks = [];
    for (const character of source) {
      buffer = (buffer << 6) | alphabet.indexOf(character);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        output += String.fromCharCode((buffer >> bits) & 255);
        if (output.length >= 4096) { chunks.push(output); output = ''; }
      }
    }
    chunks.push(output);
    return chunks.join('');
  };
})();
