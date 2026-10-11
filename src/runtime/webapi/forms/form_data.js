// Ordered multipart entries shared by JS requests and ordinary form state.
const __formEntries = new WeakMap();
globalThis.FormData = class FormData {
  constructor(form, submitter) {
    __formEntries.set(this, []);
    if (form === undefined) return;
    if (!(form instanceof DomNode) || form.tagName !== 'FORM') throw new TypeError(
      'Form required');
    if (submitter && (submitter.form !== form || submitter.type !== 'submit'))
      throw new DOMException('Invalid submitter', 'NotFoundError');
    for (const field of form.elements) {
      if (!field.name || __isControlDisabled(field) || ['button', 'reset'].includes(field.type))
        continue;
      if (field.type === 'submit' && field !== submitter) continue;
      if (['checkbox', 'radio'].includes(field.type) && !field.checked) continue;
      if (field.type === 'file') throw new DOMException('File input collection is unsupported',
        'NotSupportedError'); // Local file selection is outside this product.
      if (field.tagName === 'SELECT') {
        if (field.multiple) throw new DOMException('Multiple selects are unsupported',
          'NotSupportedError');
        for (const option of field.options)
          if (option.selected && !option.disabled && !option.closest('optgroup[disabled]'))
            this.append(field.name, option.value);
      } else this.append(field.name, field.value);
    }
  }
  append(name, value, filename) {
    if (arguments.length < 2) throw new TypeError('Name and value required');
    if (filename !== undefined && !(value instanceof Blob)) throw new TypeError(
      'Filename requires a Blob');
    if (value instanceof Blob && !(value instanceof File && filename === undefined)) value =
      new File([value], filename === undefined ? 'blob' : String(filename), {
        type: value.type,
        lastModified: value instanceof File ? value.lastModified : Date.now()
      });
    else if (!(value instanceof Blob)) value = String(value);
    const entries = __formEntries.get(this);
    if (entries.length >= 1024) throw new RangeError('FormData exceeds 1024 entries');
    entries.push([String(name), value]);
  }
  set(name, value, filename) {
    name = String(name);
    const temp = new FormData();
    temp.append(name, value, filename);
    const entry = __formEntries.get(temp)[0],
      entries = __formEntries.get(this);
    const index = entries.findIndex(pair => pair[0] === name);
    if (index < 0) {
      if (entries.length >= 1024) throw new RangeError('FormData exceeds 1024 entries');
      entries.push(entry);
      return;
    }
    entries[index] = entry;
    for (let i = entries.length - 1; i > index; --i)
      if (entries[i][0] === name) entries.splice(i, 1);
  }
  get(name) {
    name = String(name);
    return __formEntries.get(this).find(pair => pair[0] === name)?.[1] ?? null;
  }
  getAll(name) {
    name = String(name);
    return __formEntries.get(this).filter(pair => pair[0] === name).map(pair => pair[1]);
  }
  has(name) {
    name = String(name);
    return __formEntries.get(this).some(pair => pair[0] === name);
  }
  delete(name) {
      name = String(name);
      const entries = __formEntries.get(this);
      for (let i = entries.length - 1; i >= 0; --i)
        if (entries[i][0] === name) entries.splice(i, 1);
    }
    * entries() {
      for (const [name, value] of __formEntries.get(this)) yield [name, value];
    }
    * keys() {
      for (const [name] of this) yield name;
    }
    * values() {
      for (const [, value] of this) yield value;
    }
    [Symbol.iterator]() {
      return this.entries();
    }
  forEach(callback, self) {
    for (const [name, value] of this) callback.call(self, value, name, this);
  }
};

function __encodeFormData(form) {
  const boundary = '----webcli-' + crypto.randomUUID(),
    chunks = [];
  const normalize = value => String(value).replace(/\r\n|\r|\n/g, '\r\n');
  const quoted = value => normalize(value).replace(/[\r\n"]/g, c => c === '"' ? '%22' : c === '\r' ?
    '%0D' : '%0A');
  for (const [name, value] of form) {
    let head = '--' + boundary + '\r\nContent-Disposition: form-data; name="' + quoted(name) + '"';
    if (value instanceof Blob) {
      head += '; filename="' + quoted(value.name) + '"\r\nContent-Type: ' + (value.type ||
        'application/octet-stream');
    }
    chunks.push(__binaryBytes(head + '\r\n\r\n'));
    chunks.push(value instanceof Blob ? __blobBytes(value) : __binaryBytes(normalize(value)));
    chunks.push(__binaryBytes('\r\n'));
  }
  chunks.push(__binaryBytes('--' + boundary + '--\r\n'));
  return {
    bytes: __joinBytes(chunks),
    type: 'multipart/form-data; boundary=' + boundary
  };
}
