// Adapt platform values to QuickJS's graph serializer without marker properties
// on user objects. The side table identifies placeholders by object identity.
const __historyValue = (() => {
  const errors =
      new Map([Error, EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError].map(
          type => [type.name, type]));
  const walk = (root, seen, special, packing) => {
    const visit = input => {
      if (input === null || typeof input !== 'object') return input;
      if (seen.has(input)) return seen.get(input);
      if (packing && (input instanceof Blob || input instanceof Error || input instanceof RegExp)) {
        const target = {};
        seen.set(input, target);
        if (input instanceof Blob) {
          special.push({
            target,
            kind: input instanceof File ? 'File' : 'Blob',
            data: __blobBytes(input),
            type: input.type,
            name: input instanceof File ? input.name : undefined,
            lastModified: input instanceof File ? input.lastModified : undefined
          });
        } else if (input instanceof RegExp) {
          // Serialize source/flags ourselves. Some QuickJS-NG releases omit
          // RegExp from their reader's graph-reference table.
          special.push({target, kind: 'RegExp', source: input.source, flags: input.flags});
        } else {
          const item =
              {target, kind: 'Error', name: input.name, message: input.message, stack: input.stack};
          special.push(item);
          if ('cause' in input) item.cause = visit(input.cause);
        }
        return target;
      }
      if (input instanceof ArrayBuffer || ArrayBuffer.isView(input) || input instanceof Date ||
          input instanceof RegExp || input instanceof Number || input instanceof String ||
          input instanceof Boolean || Object.prototype.toString.call(input) === '[object BigInt]') {
        seen.set(input, input);
        return input;
      }
      const output = packing ? (input instanceof Map     ? new Map() :
                                    input instanceof Set ? new Set() :
                                    Array.isArray(input) ? new Array(input.length) :
                                                           {}) :
                               input;
      seen.set(input, output);
      if (input instanceof Map) {
        // Decoding changes keys in place; materialize before clearing the map.
        const entries = [...Map.prototype.entries.call(input)];
        if (!packing) output.clear();
        for (const [key, value] of entries) output.set(visit(key), visit(value));
      } else if (input instanceof Set) {
        const values = [...Set.prototype.values.call(input)];
        if (!packing) output.clear();
        for (const value of values) output.add(visit(value));
      } else {
        for (const key of Object.keys(input))
          Object.defineProperty(
              output, key,
              {value: visit(input[key]), enumerable: true, configurable: true, writable: true});
      }
      return output;
    };
    return visit(root);
  };
  return Object.freeze({
    pack(value, hasPlatformValues = true) {
      const special = [];
      if (!hasPlatformValues) return {root: value, special};
      return {root: walk(value, new Map(), special, true), special};
    },
    unpack(envelope) {
      if (envelope === null) return null;
      if (!envelope.special.length) return envelope.root;
      const seen = new Map();
      for (const item of envelope.special) {
        let value;
        switch (item.kind) {
          case 'File':
            value = new File(
                [item.data], item.name, {type: item.type, lastModified: item.lastModified});
            break;
          case 'Blob':
            value = new Blob([item.data], {type: item.type});
            break;
          case 'RegExp':
            value = new RegExp(item.source, item.flags);
            break;
          case 'Error':
            value = new (errors.get(item.name) || Error)(item.message);
            break;
          default:
            throw new DOMException('Unsupported stored value', 'DataCloneError');
        }
        if (item.kind === 'Error' && item.stack !== undefined) value.stack = item.stack;
        seen.set(item.target, value);
      }
      const root = walk(envelope.root, seen, null, false);
      for (const item of envelope.special)
        if (item.kind === 'Error' && 'cause' in item)
          seen.get(item.target).cause = walk(item.cause, seen, null, false);
      return root;
    }
  });
})();
