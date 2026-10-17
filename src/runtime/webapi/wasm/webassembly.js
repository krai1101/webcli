// Browser-facing bindings; all executable bytes stay in the native interpreter.
(() => {
  const handles = new WeakMap(), moduleBytes = new WeakMap(), functions = new WeakMap(), functionWrappers = new WeakMap(), tableKinds = new WeakMap();
  const errors = {};
  for (const name of ['CompileError', 'LinkError', 'RuntimeError']) {
    errors[name] = class extends Error { constructor(message) { super(message); this.name = name; } };
    Object.defineProperty(errors[name], 'name', {value: name});
  }
  function native(...args) {
    try { return __webcliWasm(...args); }
    catch (error) {
      if (error instanceof Error && errors[error.name])
        Object.setPrototypeOf(error, errors[error.name].prototype);
      throw error;
    }
  }
  function handle(value, type) {
    if (!(value instanceof type) || !handles.has(value)) throw new TypeError('Illegal WebAssembly invocation');
    return handles.get(value);
  }
  function bytes(value) {
    if (value instanceof ArrayBuffer) return new Uint8Array(value).slice();
    if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
    throw new TypeError('WebAssembly requires a BufferSource');
  }
  function unsigned(value, maximum = 0xffffffff) {
    if (typeof value === 'bigint' || typeof value === 'symbol') throw new TypeError('WebAssembly size requires a number');
    const number = Number(value);
    if (!Number.isFinite(number) || number < 0 || Math.trunc(number) > maximum)
      throw new RangeError('Invalid WebAssembly size');
    return Math.trunc(number);
  }
  function leb(value) {
    const result = [];
    do { const low = value & 127; value = Math.floor(value / 128); result.push(low | (value ? 128 : 0)); } while (value);
    return result;
  }
  function section(id, payload) { return [id, ...leb(payload.length), ...payload]; }
  function resource(kind, definition) {
    const binary = Uint8Array.from([0,97,115,109,1,0,0,0,
      ...section(kind === 'memory' ? 5 : kind === 'table' ? 4 : 6, [1, ...definition]),
      ...section(7, [1,5,118,97,108,117,101, kind === 'memory' ? 2 : kind === 'table' ? 1 : 3,0])]);
    return new Instance(new Module(binary)).exports.value;
  }
  function limits(descriptor, maximum) {
    if (!descriptor || typeof descriptor !== 'object') throw new TypeError('WebAssembly descriptor required');
    if (descriptor.shared || descriptor.address === 'i64') throw new TypeError('Shared memory and memory64 are unsupported');
    if (descriptor.initial === undefined) throw new TypeError('Initial size required');
    const initial = unsigned(descriptor.initial, maximum);
    if (descriptor.maximum === undefined) return [0, ...leb(initial)];
    const limit = unsigned(descriptor.maximum, maximum);
    if (initial > limit) throw new RangeError('Initial size exceeds maximum');
    return [1, ...leb(initial), ...leb(limit)];
  }
  class Module {
    constructor(source) {
      const data = bytes(source);
      handles.set(this, native('compile', data)); moduleBytes.set(this, data);
    }
    static imports(module) { return native('imports', handle(module, Module)); }
    static exports(module) { return native('exports', handle(module, Module)); }
    static customSections(module, name) {
      handle(module, Module); name = String(name);
      const data = moduleBytes.get(module), result = [];
      let position = 8;
      const read = () => {
        let value = 0, shift = 0, byte;
        do { byte = data[position++]; value += (byte & 127) * 2 ** shift; shift += 7; } while (byte & 128);
        return value;
      };
      while (position < data.length) {
        const id = data[position++], end = read() + position;
        if (id === 0) {
          const length = read(), sectionName = new TextDecoder().decode(data.subarray(position, position + length));
          position += length;
          if (sectionName === name) result.push(data.slice(position, end).buffer);
        }
        position = end;
      }
      return result;
    }
  }
  function exportedFunction(value, name = '') {
    if (functionWrappers.has(value)) return functionWrappers.get(value);
    const fn = (...args) => native('call', value, args);
    Object.defineProperty(fn, 'name', {value: name, configurable: true});
    functions.set(fn, value); functionWrappers.set(value, fn); return fn;
  }
  function wrap(value, kind, name) {
    if (kind === 'function') return exportedFunction(value, name);
    const type = {memory: Memory, table: Table, global: Global}[kind];
    const object = Object.create(type.prototype); handles.set(object, value);
    if (kind === 'table') tableKinds.set(object, native('tableType', value));
    return object;
  }
  class Instance {
    constructor(module, imports = {}) {
      const value = native('instantiate', handle(module, Module), imports);
      handles.set(this, value);
      const descriptions = Module.exports(module), values = native('instanceExports', value);
      const exports = Object.create(null);
      for (let i = 0; i < descriptions.length; ++i) {
        const item = descriptions[i];
        Object.defineProperty(exports, item.name, {enumerable: true, value: wrap(values[i], item.kind, item.name)});
      }
      Object.defineProperty(this, 'exports', {value: Object.freeze(exports), enumerable: true});
    }
  }
  class Memory {
    constructor(descriptor) { return resource('memory', limits(descriptor, 65536)); }
    get buffer() { return native('buffer', handle(this, Memory)); }
    grow(delta) { return native('memoryGrow', handle(this, Memory), unsigned(delta)); }
  }
  class Table {
    constructor(descriptor, value) {
      const kind = descriptor?.element;
      if (!['anyfunc', 'funcref', 'externref'].includes(kind)) throw new TypeError('Unsupported table element type');
      const object = resource('table', [kind === 'externref' ? 0x6f : 0x70, ...limits(descriptor, 0xffffffff)]);
      if (arguments.length < 2) value = kind === 'externref' ? undefined : null;
      if (value !== null) for (let i = 0; i < object.length; ++i) object.set(i, value);
      return object;
    }
    get length() { return native('length', handle(this, Table)); }
    get(index) {
      const value = native('get', handle(this, Table), unsigned(index));
      return native('isFunction', value) ? exportedFunction(value) : value;
    }
    set(index, value) {
      if (arguments.length < 2 && tableKinds.get(this) === 'funcref') value = null;
      return native('set', handle(this, Table), unsigned(index), functions.get(value) || value);
    }
    grow(delta, value) {
      if (arguments.length < 2 && tableKinds.get(this) === 'funcref') value = null;
      return native('grow', handle(this, Table), unsigned(delta), functions.get(value) || value);
    }
  }
  class Global {
    constructor(descriptor, value) {
      const types = {i32: 0x7f, i64: 0x7e, f32: 0x7d, f64: 0x7c};
      const type = descriptor?.value;
      if (!Object.hasOwn(types, type)) throw new TypeError('Unsupported global value type');
      if (type !== 'i64' && typeof value === 'bigint') throw new TypeError('Only i64 globals accept BigInt');
      let constant;
      if (type === 'i32' || type === 'i64') {
        let number = type === 'i64' ? BigInt.asIntN(64, value === undefined ? 0n : value) : BigInt(Number(value) | 0);
        constant = [type === 'i32' ? 0x41 : 0x42];
        while (true) {
          const byte = Number(number & 127n); number >>= 7n;
          const done = number === 0n && !(byte & 64) || number === -1n && !!(byte & 64);
          constant.push(byte | (done ? 0 : 128)); if (done) break;
        }
      } else {
        const buffer = new ArrayBuffer(type === 'f32' ? 4 : 8), view = new DataView(buffer);
        if (type === 'f32') view.setFloat32(0, value === undefined ? 0 : Number(value), true);
        else view.setFloat64(0, value === undefined ? 0 : Number(value), true);
        constant = [type === 'f32' ? 0x43 : 0x44, ...new Uint8Array(buffer)];
      }
      return resource('global', [types[type], descriptor.mutable ? 1 : 0, ...constant, 0x0b]);
    }
    get value() { return native('globalGet', handle(this, Global)); }
    set value(value) { native('globalSet', handle(this, Global), value); }
    valueOf() { return this.value; }
  }
  async function responseBytes(source) {
    const response = await source;
    if (!(response instanceof Response) || !response.ok ||
        !['default', 'basic', 'cors'].includes(response.type) ||
        response.headers.get('content-type')?.split(';')[0].trim().toLowerCase() !== 'application/wasm')
      throw new TypeError('WebAssembly streaming requires an OK application/wasm Response');
    return response.arrayBuffer();
  }
  const api = {
    Module, Instance, Memory, Table, Global, ...errors,
    validate(source) { const data = bytes(source); return native('validate', data); },
    async compile(source) { return new Module(source); },
    async instantiate(source, imports) {
      if (source instanceof Module) return new Instance(source, imports);
      const module = new Module(source); return {module, instance: new Instance(module, imports)};
    },
    async compileStreaming(source) { return new Module(await responseBytes(source)); },
    async instantiateStreaming(source, imports) { return api.instantiate(await responseBytes(source), imports); }
  };
  Object.defineProperty(api, Symbol.toStringTag, {value: 'WebAssembly'});
  for (const type of [Module, Instance, Memory, Table, Global])
    Object.defineProperty(type.prototype, Symbol.toStringTag, {value: 'WebAssembly.' + type.name});
  globalThis.WebAssembly = api;
})();
