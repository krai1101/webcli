// DOM boundary points use child indexes or CharacterData UTF-16 offsets.
const __rangeStates = new WeakMap();
const __liveRanges = new Set();
function __rangeState(range) {
  const state = __rangeStates.get(range);
  if (!state) throw new TypeError('Range required');
  return state;
}
function __rangeLength(node) {
  return node instanceof CharacterData ? node.data.length : node.childNodes.length;
}
function __rangePoint(node, offset) {
  if (!(node instanceof Node)) throw new TypeError('Node required');
  if (node.nodeType === 10)
    throw new DOMException('Doctype is not a boundary', 'InvalidNodeTypeError');
  offset = Number(offset) >>> 0;
  if (offset > __rangeLength(node))
    throw new DOMException('Offset exceeds node length', 'IndexSizeError');
  return {node, offset};
}
function __rangeCompare(a, b) {
  if (a.node === b.node) return Math.sign(a.offset - b.offset);
  if (a.node.getRootNode() !== b.node.getRootNode())
    throw new DOMException('Different trees', 'WrongDocumentError');
  if (a.node.contains(b.node)) {
    let child = b.node;
    while (child.parentNode !== a.node) child = child.parentNode;
    return a.offset <= a.node.childNodes.indexOf(child) ? -1 : 1;
  }
  if (b.node.contains(a.node)) return -__rangeCompare(b, a);
  return a.node.compareDocumentPosition(b.node) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
}
function __rangeCommon(a, b) {
  while (!a.contains(b)) a = a.parentNode;
  return a;
}
function __rangeSibling(node, after) {
  if (!(node instanceof Node)) throw new TypeError('Node required');
  const parent = node.parentNode;
  if (!parent) throw new DOMException('Node has no parent', 'InvalidNodeTypeError');
  return {node: parent, offset: parent.childNodes.indexOf(node) + (after ? 1 : 0)};
}
function __newRange(owner) {
  const range = new Range();
  const state = __rangeState(range);
  state.start = {node: owner, offset: 0};
  state.end = {...state.start};
  return range;
}
globalThis.Range = class Range {
  constructor() {
    __rangeStates.set(this, {start: {node: document, offset: 0}, end: {node: document, offset: 0}});
    __liveRanges.add(new WeakRef(this));
  }
  get startContainer() {
    return __rangeState(this).start.node;
  }
  get startOffset() {
    return __rangeState(this).start.offset;
  }
  get endContainer() {
    return __rangeState(this).end.node;
  }
  get endOffset() {
    return __rangeState(this).end.offset;
  }
  get collapsed() {
    const {start, end} = __rangeState(this);
    return start.node === end.node && start.offset === end.offset;
  }
  get commonAncestorContainer() {
    const {start, end} = __rangeState(this);
    return __rangeCommon(start.node, end.node);
  }
  setStart(node, offset) {
    const point = __rangePoint(node, offset), state = __rangeState(this);
    if (point.node.getRootNode() !== state.end.node.getRootNode() ||
        __rangeCompare(point, state.end) > 0)
      state.end = {...point};
    state.start = point;
  }
  setEnd(node, offset) {
    const point = __rangePoint(node, offset), state = __rangeState(this);
    if (point.node.getRootNode() !== state.start.node.getRootNode() ||
        __rangeCompare(point, state.start) < 0)
      state.start = {...point};
    state.end = point;
  }
  setStartBefore(node) {
    const p = __rangeSibling(node, false);
    this.setStart(p.node, p.offset);
  }
  setStartAfter(node) {
    const p = __rangeSibling(node, true);
    this.setStart(p.node, p.offset);
  }
  setEndBefore(node) {
    const p = __rangeSibling(node, false);
    this.setEnd(p.node, p.offset);
  }
  setEndAfter(node) {
    const p = __rangeSibling(node, true);
    this.setEnd(p.node, p.offset);
  }
  selectNode(node) {
    this.setStartBefore(node);
    this.setEndAfter(node);
  }
  selectNodeContents(node) {
    this.setStart(node, 0);
    this.setEnd(node, __rangeLength(node));
  }
  collapse(toStart = false) {
    const state = __rangeState(this);
    if (toStart)
      state.end = {...state.start};
    else
      state.start = {...state.end};
  }
  detach() {
    __rangeState(this);
  }
  cloneRange() {
    const state = __rangeState(this),
          clone = __newRange(state.start.node.ownerDocument || state.start.node);
    Object.assign(__rangeState(clone), {start: {...state.start}, end: {...state.end}});
    return clone;
  }
  compareBoundaryPoints(how, other) {
    const state = __rangeState(this), source = __rangeState(other);
    const pairs = [
      [state.start, source.start], [state.end, source.start], [state.end, source.end],
      [state.start, source.end]
    ];
    how = (Number(how) >>> 0) & 65535;
    if (how > 3) throw new DOMException('Unknown boundary comparison', 'NotSupportedError');
    return __rangeCompare(...pairs[how]);
  }
  comparePoint(node, offset) {
    const point = __rangePoint(node, offset), {start, end} = __rangeState(this);
    return __rangeCompare(point, start) < 0 ? -1 : __rangeCompare(point, end) > 0 ? 1 : 0;
  }
  isPointInRange(node, offset) {
    __rangePoint(node, offset);
    return node.getRootNode() === this.startContainer.getRootNode() &&
        this.comparePoint(node, offset) === 0;
  }
  intersectsNode(node) {
    if (!(node instanceof Node)) throw new TypeError('Node required');
    const {start, end} = __rangeState(this);
    if (node.getRootNode() !== start.node.getRootNode()) return false;
    return !node.parentNode ||
        (__rangeCompare(__rangeSibling(node, false), end) < 0 &&
         __rangeCompare(__rangeSibling(node, true), start) > 0);
  }
};
for (const [name, value] of Object.entries(
         {START_TO_START: 0, START_TO_END: 1, END_TO_END: 2, END_TO_START: 3})) {
  Object.defineProperty(Range, name, {value, enumerable: true});
  Object.defineProperty(Range.prototype, name, {value, enumerable: true});
}
Object.defineProperty(Range.prototype, Symbol.toStringTag, {value: 'Range', configurable: true});
