// Character data uses the existing Lexbor-backed Node text accessors. String
// offsets are UTF-16 code units, as in JavaScript and the DOM specification.
globalThis.CharacterData = class CharacterData extends Node {
  constructor() {
    throw new TypeError('Illegal constructor');
  }
  get data() {
    return this.textContent;
  }
  set data(value) {
    this.replaceData(0, this.length, value === null ? '' : String(value));
  }
  get length() {
    return this.data.length;
  }
  substringData(offset, count) {
    offset = __characterOffset(this, offset);
    return this.data.slice(offset, offset + (Number(count) >>> 0));
  }
  appendData(value) {
    this.replaceData(this.length, 0, value);
  }
  insertData(offset, value) {
    this.replaceData(offset, 0, value);
  }
  deleteData(offset, count) {
    this.replaceData(offset, count, '');
  }
  replaceData(offset, count, value) {
    offset = __characterOffset(this, offset);
    const data = this.data;
    count = Math.min(Number(count) >>> 0, data.length - offset);
    value = String(value);
    __webcliTextSet(this.__id, data.slice(0, offset) + value + data.slice(offset + count));
    __notify(this, 'characterData', {oldValue: data}, {offset, count, length: value.length});
  }
};
function __characterOffset(node, value) {
  if (!(node instanceof CharacterData)) throw new TypeError('CharacterData required');
  const offset = Number(value) >>> 0;
  if (offset > node.data.length)
    throw new DOMException('Offset exceeds data length', 'IndexSizeError');
  return offset;
}
globalThis.Text = class Text extends CharacterData {
  constructor(data = '') {
    return document.createTextNode(String(data));
  }
  splitText(offset) {
    offset = __characterOffset(this, offset);
    const data = this.data, parent = this.parentNode;
    const index = parent ? parent.childNodes.indexOf(this) : -1;
    const next = this.ownerDocument.createTextNode(data.slice(offset));
    if (parent) parent.insertBefore(next, this.nextSibling);
    __webcliTextSet(this.__id, data.slice(0, offset));
    __notify(
        this, 'characterData', {oldValue: data},
        {split: next, parent, index, offset, count: data.length - offset, length: 0});
    return next;
  }
};
globalThis.Comment = class Comment extends CharacterData {
  constructor(data = '') {
    return document.createComment(String(data));
  }
};
globalThis.DocumentType = class DocumentType extends Node {
  constructor() {
    throw new TypeError('Illegal constructor');
  }
  get name() {
    return __webcliNodeName(this.__id);
  }
};
