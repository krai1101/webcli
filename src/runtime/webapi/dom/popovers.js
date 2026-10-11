// Nonvisual popover state. Native visibility and JS selectors share this state.
const __openPopovers = new Set();
const __popoverTasks = new WeakMap();
const __popoverInvokers = new WeakMap();
function __popoverMode(node) {
  const value = node.getAttribute('popover');
  if (value === null) return null;
  return ['manual', 'hint'].includes(value.toLowerCase()) ? value.toLowerCase() : 'auto';
}
function __popoverOpen(node) {
  return node instanceof HTMLElement && __webcliPopoverState(node.__id);
}
function __prunePopovers() {
  for (const node of __openPopovers)
    if (!node.isConnected || __popoverMode(node) === null) {
      __openPopovers.delete(node);
      __webcliPopoverState(node.__id, false);
    }
}
function __changePopover(node, open, source = null) {
  if (!(node instanceof HTMLElement)) throw new TypeError('HTMLElement required');
  const mode = __popoverMode(node);
  if (mode === null) throw new DOMException('Element has no popover attribute', 'NotSupportedError');
  __prunePopovers();
  const previous = __popoverOpen(node);
  if (previous === open) return previous;
  if (!node.isConnected || node.ownerDocument !== document)
    throw new DOMException('Popover has no active document', 'InvalidStateError');
  const init = {oldState: previous ? 'open' : 'closed', newState: open ? 'open' : 'closed', source};
  if (!__dispatchInputEvent(node, new ToggleEvent('beforetoggle', {...init, cancelable: open})))
    return previous;
  if (!node.isConnected || __popoverMode(node) !== mode)
    throw new DOMException('Popover changed during beforetoggle', 'InvalidStateError');
  if (__popoverOpen(node) !== previous) return __popoverOpen(node);
  if (open && mode !== 'manual')
    for (const other of [...__openPopovers].reverse())
      if (other !== node && other.ownerDocument === node.ownerDocument &&
          __popoverMode(other) !== 'manual' && !other.contains(node))
        __changePopover(other, false);
  if (!node.isConnected || __popoverMode(node) !== mode)
    throw new DOMException('Popover changed while closing other popovers', 'InvalidStateError');
  __webcliPopoverState(node.__id, open);
  if (open) __openPopovers.add(node);
  else __openPopovers.delete(node);
  const pending = __popoverTasks.get(node);
  if (pending) {pending.newState = init.newState; pending.source = source;}
  else {
    __popoverTasks.set(node, init);
    setTimeout(() => {
      __popoverTasks.delete(node);
      __dispatchInputEvent(node, new ToggleEvent('toggle', init));
    }, 0);
  }
  return open;
}
Object.defineProperty(HTMLElement.prototype, 'popover', {
  configurable: true, enumerable: true,
  get() {return __popoverMode(this);},
  set(value) {
    if (value === null) this.removeAttribute('popover');
    else this.setAttribute('popover', String(value));
  }
});
HTMLElement.prototype.showPopover = function(options = {}) {
  __changePopover(this, true, options?.source ?? null);
};
HTMLElement.prototype.hidePopover = function() {__changePopover(this, false);};
HTMLElement.prototype.togglePopover = function(options) {
  const force = typeof options === 'boolean' ? options : options?.force;
  return __changePopover(this, force === undefined ? !__popoverOpen(this) : Boolean(force),
    typeof options === 'object' ? options?.source ?? null : null);
};
Object.defineProperties(HTMLElement.prototype, {
  popoverTargetElement: {
    configurable: true,
    get() {
      if (__popoverInvokers.has(this)) return __popoverInvokers.get(this);
      const id = this.getAttribute('popovertarget');
      return id === null ? null :
        this.getRootNode().querySelectorAll('[id]').find(n => n.id === id) ?? null;
    },
    set(value) {
      if (value !== null && !(value instanceof Element)) throw new TypeError('Element required');
      if (value === null) __popoverInvokers.delete(this);
      else __popoverInvokers.set(this, value);
    }
  },
  popoverTargetAction: {
    configurable: true,
    get() {
      const value = (this.getAttribute('popovertargetaction') ?? '').toLowerCase();
      return ['show','hide'].includes(value) ? value : 'toggle';
    },
    set(value) {this.setAttribute('popovertargetaction', String(value));}
  }
});
function __activatePopover(node) {
  if (node.tagName !== 'BUTTON' &&
      !(node.tagName === 'INPUT' && ['button','submit','reset','image'].includes(node.type))) return;
  const target = node.popoverTargetElement;
  if (!(target instanceof HTMLElement) || target.getRootNode() !== node.getRootNode() ||
      __popoverMode(target) === null) return;
  try {
    const action = node.popoverTargetAction;
    __changePopover(target, action === 'show' || action === 'toggle' && !__popoverOpen(target), node);
  } catch (error) {
    if (!(error instanceof DOMException) ||
        !['InvalidStateError','NotSupportedError'].includes(error.name)) throw error;
  }
}
function __popoverSelector(root) {
  __prunePopovers();
  const paths = [];
  for (const node of __openPopovers) {
    const tree = node.getRootNode();
    if (tree !== (root?.nodeType === 9 ? root : root?.getRootNode())) continue;
    const parts = [];
    for (let current = node; current?.parentElement; current = current.parentElement)
      parts.unshift(':nth-child(' + (current.parentElement.children.indexOf(current) + 1) + ')');
    if (tree.nodeType === 9) paths.push([':root', ...parts].join(' > '));
  }
  return paths.length ? ':is(' + paths.join(',') + ')' : ':not(*)';
}
