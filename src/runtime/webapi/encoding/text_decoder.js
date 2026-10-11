// UTF-8 decoder with streaming state, BOM handling and fatal-error mode.
{
  const state = new WeakMap();
  globalThis.TextDecoder = class TextDecoder {
    constructor(label = 'utf-8', options = {}) {
      if (!['utf-8', 'utf8', 'unicode-1-1-utf-8'].includes(String(label).trim().toLowerCase()))
        throw new RangeError('only UTF-8 decoding is supported');
      state.set(this, {fatal:!!options.fatal, ignoreBOM:!!options.ignoreBOM, pending:[], bomSeen:false});
    }
    get encoding() { if (!state.has(this)) throw new TypeError('invalid decoder'); return 'utf-8'; }
    get fatal() { if (!state.has(this)) throw new TypeError('invalid decoder'); return state.get(this).fatal; }
    get ignoreBOM() { if (!state.has(this)) throw new TypeError('invalid decoder'); return state.get(this).ignoreBOM; }
    decode(input = undefined, options = {}) {
      const current = state.get(this);
      if (!current) throw new TypeError('invalid TextDecoder receiver');
      let bytes;
      if (input === undefined) bytes = new Uint8Array(0);
      else if (ArrayBuffer.isView(input)) bytes = new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
      else if (input instanceof ArrayBuffer || (typeof SharedArrayBuffer !== 'undefined' && input instanceof SharedArrayBuffer)) bytes = new Uint8Array(input);
      else throw new TypeError('decode requires an ArrayBuffer or view');
      if (current.pending.length) {
        const combined = new Uint8Array(current.pending.length + bytes.length);
        combined.set(current.pending); combined.set(bytes, current.pending.length); bytes = combined;
      }
      current.pending = [];
      const chunks = [];
      let output = '', at = 0;
      const emit = point => {
        if (!current.bomSeen) {current.bomSeen = true; if (!current.ignoreBOM && point === 0xfeff) return;}
        output += String.fromCodePoint(point);
        if (output.length >= 4096) {chunks.push(output); output = '';}
      };
      const invalid = () => {
        if (current.fatal) {current.bomSeen = false; throw new TypeError('invalid UTF-8 data');}
        emit(0xfffd);
      };
      while (at < bytes.length) {
        const start = at, first = bytes[at++];
        if (first < 0x80) {emit(first); continue;}
        const count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
        if (!count) {invalid(); continue;}
        let point = first & (count === 1 ? 31 : count === 2 ? 15 : 7), valid = true;
        for (let i = 0; i < count; ++i) {
          if (at === bytes.length) {
            if (options.stream) {current.pending = Array.from(bytes.subarray(start)); valid = false; break;}
            invalid(); valid = false; break;
          }
          const next = bytes[at];
          const lower = i === 0 && first === 0xe0 ? 0xa0 : i === 0 && first === 0xf0 ? 0x90 : 0x80;
          const upper = i === 0 && first === 0xed ? 0x9f : i === 0 && first === 0xf4 ? 0x8f : 0xbf;
          if (next < lower || next > upper) {invalid(); valid = false; break;}
          point = (point << 6) | (next & 63); ++at;
        }
        if (valid) emit(point);
      }
      if (!options.stream) current.bomSeen = false;
      chunks.push(output);
      return chunks.join('');
    }
    get [Symbol.toStringTag]() { return 'TextDecoder'; }
  };
}
