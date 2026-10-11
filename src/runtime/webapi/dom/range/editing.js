function __rangeContained(node, start, end) {
  return node.parentNode && __rangeCompare(start, __rangeSibling(node, false)) <= 0 &&
      __rangeCompare(__rangeSibling(node, true), end) <= 0;
}
function __rangeCollapsePoint(start, end) {
  if (start.node.contains(end.node)) return {...start};
  let node = start.node;
  while (node.parentNode && !node.parentNode.contains(end.node)) node = node.parentNode;
  return __rangeSibling(node, true);
}
function __rangeTopNodes(common, start, end) {
  const result = [], pending = [...common.childNodes].reverse();
  while (pending.length) {
    const node = pending.pop();
    if (__rangeContained(node, start, end))
      result.push(node);
    else if (node.contains(start.node) || node.contains(end.node))
      pending.push(...[...node.childNodes].reverse());
  }
  return result;
}
Range.prototype.deleteContents = function() {
  const state = __rangeState(this);
  if (this.collapsed) return;
  const start = {...state.start}, end = {...state.end};
  if (start.node === end.node && start.node instanceof CharacterData) {
    start.node.deleteData(start.offset, end.offset - start.offset);
    this.setStart(start.node, start.offset);
    this.collapse(true);
    return;
  }
  const nodes = __rangeTopNodes(this.commonAncestorContainer, start, end);
  const collapsed = __rangeCollapsePoint(start, end);
  if (start.node instanceof CharacterData)
    start.node.deleteData(start.offset, start.node.length - start.offset);
  for (const node of nodes) node.remove();
  if (end.node instanceof CharacterData) end.node.deleteData(0, end.offset);
  this.setStart(collapsed.node, collapsed.offset);
  this.collapse(true);
};
// Partial ancestors are copied as shells; fully selected nodes are either cloned
// or moved. Snapshot the selection plan before mutation changes live boundaries.
function __rangeContentPlan(node, start, end) {
  if (__rangeContained(node, start, end)) return {node, whole: true};
  if (node instanceof CharacterData) {
    if (node !== start.node && node !== end.node) return null;
    return {
      node,
      from: node === start.node ? start.offset : 0,
      to: node === end.node ? end.offset : node.length
    };
  }
  const children =
      node.childNodes.map(child => __rangeContentPlan(child, start, end)).filter(Boolean);
  return children.length ? {node, children} : null;
}
function __rangeWritePlan(plan, parent, extract) {
  if (plan.whole) {
    if (plan.node.nodeType === 10)
      throw new DOMException('Cannot copy a doctype', 'HierarchyRequestError');
    parent.appendChild(extract ? plan.node : plan.node.cloneNode(true));
  } else if (plan.children) {
    const shell = plan.node.cloneNode(false);
    parent.appendChild(shell);
    for (const child of plan.children) __rangeWritePlan(child, shell, extract);
  } else {
    const copy = plan.node.cloneNode(false);
    copy.data = plan.node.data.slice(plan.from, plan.to);
    parent.appendChild(copy);
    if (extract) plan.node.deleteData(plan.from, plan.to - plan.from);
  }
}
function __rangeContents(range, extract) {
  const {start, end} = __rangeState(range);
  const owner = start.node.ownerDocument || start.node;
  const fragment = owner.createDocumentFragment();
  if (range.collapsed) return fragment;
  const collapsed = __rangeCollapsePoint(start, end);
  const plans = start.node === end.node && start.node instanceof CharacterData ?
      [{node: start.node, from: start.offset, to: end.offset}] :
      range.commonAncestorContainer.childNodes.map(node => __rangeContentPlan(node, start, end))
          .filter(Boolean);
  if (plans.some(plan => plan.whole && plan.node.nodeType === 10))
    throw new DOMException('Cannot copy a doctype', 'HierarchyRequestError');
  for (const plan of plans) __rangeWritePlan(plan, fragment, extract);
  if (extract) {
    range.setStart(collapsed.node, collapsed.offset);
    range.collapse(true);
  }
  return fragment;
}
Range.prototype.cloneContents = function() {
  return __rangeContents(this, false);
};
Range.prototype.extractContents = function() {
  return __rangeContents(this, true);
};
Range.prototype.insertNode = function(node) {
  if (!(node instanceof Node)) throw new TypeError('Node required');
  const {start} = __rangeState(this), collapsed = this.collapsed;
  if (start.node.nodeType === 8 || start.node.nodeType === 7 ||
      (start.node instanceof Text && !start.node.parentNode) || node === start.node)
    throw new DOMException('Invalid insertion boundary', 'HierarchyRequestError');
  let reference =
      start.node instanceof Text ? start.node : start.node.childNodes[start.offset] || null;
  const parent = start.node instanceof Text ? start.node.parentNode : start.node;
  for (let ancestor = parent; ancestor; ancestor = ancestor.parentNode)
    if (ancestor === node)
      throw new DOMException('Cannot insert an ancestor', 'HierarchyRequestError');
  if (start.node instanceof Text) reference = start.node.splitText(start.offset);
  if (reference === node) reference = node.nextSibling;
  if (node.parentNode) node.remove();
  const offset = reference ? parent.childNodes.indexOf(reference) : parent.childNodes.length;
  const count = node.nodeType === 11 ? node.childNodes.length : 1;
  parent.insertBefore(node, reference);
  if (collapsed) this.setEnd(parent, offset + count);
};
Range.prototype.surroundContents = function(parent) {
  if (!(parent instanceof Element))
    throw new DOMException('Element required', 'InvalidNodeTypeError');
  const {start, end} = __rangeState(this), common = this.commonAncestorContainer;
  for (const point of [start, end])
    for (let node = point.node; node !== common; node = node.parentNode)
      if (!(node instanceof Text) && node.contains(start.node) !== node.contains(end.node))
        throw new DOMException('Partially selected non-text node', 'InvalidStateError');
  const contents = this.extractContents();
  parent.replaceChildren();
  this.insertNode(parent);
  parent.appendChild(contents);
  this.selectNode(parent);
};
Range.prototype.toString = function() {
  const {start, end} = __rangeState(this);
  if (this.collapsed) return '';
  const pending = [this.commonAncestorContainer], text = [];
  while (pending.length) {
    const node = pending.pop();
    if (node instanceof Text) {
      if (node === start.node || node === end.node || __rangeContained(node, start, end))
        text.push(node.data.slice(
            node === start.node ? start.offset : 0, node === end.node ? end.offset : node.length));
    } else
      pending.push(...[...node.childNodes].reverse());
  }
  return text.join('');
};
Range.prototype.createContextualFragment = function(html) {
  const {start} = __rangeState(this), owner = start.node.ownerDocument || start.node;
  const context = start.node instanceof Element ? start.node : start.node.parentElement;
  const shell = context && context.tagName !== 'HTML' ? context.cloneNode(false) :
                                                        owner.createElement('body');
  shell.innerHTML = String(html);
  // Unlike innerHTML, contextual fragments keep scripts eligible to execute
  // when the fragment is subsequently connected to the document.
  for (const script of shell.querySelectorAll('script')) script._executed = false;
  const fragment = owner.createDocumentFragment();
  while (shell.firstChild) fragment.appendChild(shell.firstChild);
  return fragment;
};
