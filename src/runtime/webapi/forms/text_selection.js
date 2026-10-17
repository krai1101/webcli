// Text control offsets are UTF-16 code units, as in JavaScript strings.
const __textSelections = new WeakMap();
const __pendingSelectionChanges = new WeakSet();
function __supportsTextSelection(node) {
  return node.tagName === 'TEXTAREA' ||
      ['text', 'search', 'url', 'tel', 'password'].includes(node.type);
}
function __textSelection(node) {
  let state = __textSelections.get(node);
  if (!state) {
    state = {start: 0, end: 0, direction: 'none'};
    __textSelections.set(node, state);
  }
  const length = node.value.length;
  state.start = Math.min(state.start, length);
  state.end = Math.min(state.end, length);
  return state;
}
function __setTextSelection(node, start, end, direction) {
  if (!__supportsTextSelection(node))
    throw new DOMException('This control does not support selection', 'InvalidStateError');
  const state = __textSelection(node);
  const length = node.value.length;
  end = Math.min(Number(end) >>> 0, length);
  start = Math.min(Number(start) >>> 0, end);
  direction = ['forward', 'backward'].includes(direction) ? direction : 'none';
  if (state.start === start && state.end === end && state.direction === direction) return;
  Object.assign(state, {start, end, direction});
  // Delivery is asynchronous so setters do not reenter page handlers.
  if (!__pendingSelectionChanges.has(node)) {
    __pendingSelectionChanges.add(node);
    setTimeout(() => {
      __pendingSelectionChanges.delete(node);
      __dispatchBrowserEvent.call(node, new Event('selectionchange', {bubbles: true}), true);
    }, 0);
  }
}
for (const prototype of [HTMLInputElement.prototype, HTMLTextAreaElement.prototype]) {
  const value = Object.getOwnPropertyDescriptor(prototype, 'value');
  Object.defineProperties(prototype, {
    value: {
      ...value,
      set(next) {
        const previous = value.get.call(this);
        value.set.call(this, next);
        if (__supportsTextSelection(this) && previous !== value.get.call(this)) {
          const length = this.value.length;
          __setTextSelection(this, length, length, 'none');
        }
      }
    },
    selectionStart: {
      configurable: true,
      get() { return __supportsTextSelection(this) ? __textSelection(this).start : null; },
      set(value) {
        const state = __textSelection(this);
        __setTextSelection(this, value, Math.max(Number(value) >>> 0, state.end), state.direction);
      }
    },
    selectionEnd: {
      configurable: true,
      get() { return __supportsTextSelection(this) ? __textSelection(this).end : null; },
      set(value) {
        const state = __textSelection(this);
        __setTextSelection(this, state.start, value, state.direction);
      }
    },
    selectionDirection: {
      configurable: true,
      get() { return __supportsTextSelection(this) ? __textSelection(this).direction : null; },
      set(value) {
        const state = __textSelection(this);
        __setTextSelection(this, state.start, state.end, String(value));
      }
    },
    setSelectionRange: {
      configurable: true, writable: true,
      value(start, end, direction = 'none') {
        __setTextSelection(this, start, end, direction);
      }
    },
    select: {
      configurable: true, writable: true,
      value() {
        if (!__supportsTextSelection(this)) return;
        this.setSelectionRange(0, this.value.length);
        this.dispatchEvent(new Event('select', {bubbles: true}));
      }
    }
  });
}
