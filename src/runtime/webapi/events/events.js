function __eventInside(target, root) {
  for (let tree = target?.getRootNode?.(); tree;
       tree = globalThis.ShadowRoot && tree instanceof ShadowRoot ?
           tree.host.getRootNode() :
           null)
    if (tree === root) return true;
  return false;
}
const __eventStates = new WeakMap();
class Event {
  constructor(type, options = {}) {
    options ??= {};
    __eventStates.set(this, {
      type: String(type),
      bubbles: !!options.bubbles,
      cancelable: !!options.cancelable,
      composed: !!options.composed
    });
    Object.defineProperty(this, 'isTrusted', {
      enumerable: true,
      get: () => __eventStates.get(this).trusted === true
    });
    this._path = [];
    this.defaultPrevented = false;
    this.cancelBubble = false;
    this.target = null;
    this.currentTarget = null;
    this.eventPhase = 0;
    this._immediate = false;
    this.timeStamp = Date.now();
  }
  get type() {
    return __eventStates.get(this).type;
  }
  get bubbles() {
    return __eventStates.get(this).bubbles;
  }
  get cancelable() {
    return __eventStates.get(this).cancelable;
  }
  get composed() {
    return __eventStates.get(this).composed;
  }
  composedPath() {
    return this._path.filter(node => {
      for (let root = node.getRootNode?.();
           root instanceof globalThis.ShadowRoot;
           root = root.host.getRootNode())
        if (root.mode === 'closed' && !__eventInside(this.currentTarget, root))
          return false;
      return true;
    });
  }
  preventDefault() {
    if (this.cancelable && !this._passive) this.defaultPrevented = true;
  }
  stopPropagation() {
    this.cancelBubble = true;
  }
  stopImmediatePropagation() {
    this._immediate = true;
    this.cancelBubble = true;
  }
  initEvent(type, bubbles = false, cancelable = false) {
    const state = __eventStates.get(this);
    if (!state) throw new TypeError('Event required');
    if (this._dispatching) return;
    state.type = String(type);
    state.bubbles = !!bubbles;
    state.cancelable = !!cancelable;
    state.trusted = false;
    this.defaultPrevented = false;
    this.cancelBubble = false;
    this._immediate = false;
    this.target = null;
  }
}
class SubmitEvent extends Event {
  constructor(type, options = {}) {
    options ??= {};
    super(type, options);
    const submitter = options.submitter ?? null;
    if (submitter !== null && !(submitter instanceof HTMLElement))
      throw new TypeError('SubmitEvent submitter must be an HTMLElement');
    Object.defineProperty(this, 'submitter', {value: submitter, enumerable: true});
  }
}
class ProgressEvent extends Event {
  constructor(type, options = {}) {
    super(type, options);
    const count = value => Number.isFinite(Number(value)) ? Math.max(0, Math.trunc(Number(value))) : 0;
    Object.defineProperties(this, {
      lengthComputable: {value: !!options.lengthComputable, enumerable: true},
      loaded: {value: count(options.loaded ?? 0), enumerable: true},
      total: {value: count(options.total ?? 0), enumerable: true}
    });
  }
}
class CustomEvent extends Event {
  constructor(type, options = {}) {
    super(type, options);
    this.detail = options.detail;
  }
  initCustomEvent(type, bubbles = false, cancelable = false, detail = null) {
    if (this._dispatching) return;
    this.initEvent(type, bubbles, cancelable);
    this.detail = detail;
  }
}
class PopStateEvent extends Event {
  constructor(type, options = {}) {
    super(type, options);
    this.state = options.state;
  }
}
class HashChangeEvent extends Event {
  constructor(type, options = {}) {
    super(type, options);
    Object.defineProperties(this, {
      oldURL: {value: String(options?.oldURL ?? ''), enumerable: true},
      newURL: {value: String(options?.newURL ?? ''), enumerable: true}
    });
  }
}
const __eventListenerStates = new WeakMap();
// Non-DOM event targets (e.g. IndexedDB) have their own propagation parents.
const __eventParents = new WeakMap();
const __eventExceptionHandlers = new WeakMap();
function __eventListeners(target) {
  const listeners = __eventListenerStates.get(target);
  if (!listeners) throw new TypeError('EventTarget required');
  return listeners;
}
function __removeEventListener(target, type, callback, capture) {
  const listeners = __eventListeners(target), list = listeners.get(type) || [];
  for (const item of list)
    if (item.callback === callback && item.capture === capture && item.signal)
      item.detachAbort();
  const remaining = list.filter(
      item => item.callback !== callback || item.capture !== capture);
  if (remaining.length)
    listeners.set(type, remaining);
  else
    listeners.delete(type);
  if (typeof __abortEngine !== 'undefined')
    __abortEngine.updateRetention(target);
}
class EventTarget {
  constructor() {
    __eventListenerStates.set(this, new Map());
  }
  addEventListener(type, callback, options = {}) {
    const listeners = __eventListeners(this);
    if (!callback) return;
    type = String(type);
    const capture = typeof options === 'boolean' ? options : !!options?.capture;
    const once = !!options?.once, passive = !!options?.passive;
    const signal = typeof options === 'object' ? options?.signal : undefined;
    if (signal != null && __abortEngine.aborted(signal)) return;
    const list = listeners.get(type) || [];
    if (list.some(
            item => item.callback === callback && item.capture === capture))
      return;
    const item = {
      callback,
      capture,
      once,
      passive,
      signal
    };
    if (signal) {
      item.detachAbort = __subscribeAbort(
          signal, () => __removeEventListener(this, type, callback, capture));
    }
    list.push(item);
    listeners.set(type, list);
    if (typeof __abortEngine !== 'undefined')
      __abortEngine.updateRetention(this);
  }
  removeEventListener(type, callback, options = {}) {
    type = String(type);
    const capture = typeof options === 'boolean' ? options : !!options?.capture;
    __removeEventListener(this, type, callback, capture);
  }
  _invoke(event, capture) {
    event.currentTarget = this;
    const call = callback => {
      try {
        return callback();
      } catch (error) {
        __eventExceptionHandlers.get(this)?.(error);
        __webcliReportError(
            'event ' + event.type + ': ' + String(error) + '\n' +
            (error?.stack || ''));
      }
    };
    const listeners = __eventListeners(this);
    for (const item of [...(listeners.get(event.type) || [])]) {
      if (item.capture !== capture ||
          !(listeners.get(event.type) || []).includes(item))
        continue;
      if (item.once)
        __removeEventListener(this, event.type, item.callback, item.capture);
      event._passive = item.passive;
      try {
        call(
            () => typeof item.callback === 'function' ?
                item.callback.call(this, event) :
                item.callback.handleEvent(event));
      } finally {
        event._passive = false;
      }
      if (event._immediate) break;
    }
    if (!capture && !event._immediate) {
      const handler = this['on' + event.type];
      if (typeof handler === 'function' &&
          call(() => handler.call(this, event)) === false)
        event.preventDefault();
      if (this.getAttribute) {
        const source = this.getAttribute('on' + event.type);
        if (source && __webcliLimits.authorScripts &&
            call(() => new Function('event', source).call(this, event)) ===
                false)
          event.preventDefault();
      }
    }
  }
  dispatchEvent(event) {
    return __dispatchBrowserEvent.call(this, event, false);
  }
}
function __dispatchBrowserEvent(event, trusted) {
  __eventListeners(this);
  if (!(event instanceof Event))
    throw new TypeError('dispatchEvent requires an Event');
  if (event._dispatching || !event.type)
    throw new DOMException('Invalid event dispatch', 'InvalidStateError');
  __eventStates.get(event).trusted = trusted;
  event._dispatching = true;
  event.cancelBubble = false;
  event._immediate = false;
  const origin = this, originRoot = this.getRootNode?.();
  const path = [];
  let node = this;
  while (node) {
    let parent =
        typeof __assignedSlot === 'function' ? __assignedSlot(node) : null;
    if (!parent) parent = __eventParents.get(node) || node.parentNode;
    if (!parent && globalThis.ShadowRoot && node instanceof ShadowRoot &&
        (event.composed || node !== originRoot))
      parent = node.host;
    if (!parent) break;
    path.push(parent);
    node = parent;
  }
  // DOM's Document parent algorithm excludes resource load events from the
  // Window path. Capturing window.load listeners must not run for every
  // script.
  if (event.type !== 'load' &&
      typeof document !== 'undefined' && (this === document || path.some(n => n === document)))
    path.push(window);
  event._path = [this, ...path];
  const invoke = (target, capture) => {
    let adjusted = origin;
    for (let root = adjusted.getRootNode?.(); globalThis.ShadowRoot &&
         root instanceof ShadowRoot && !__eventInside(target, root);
         root = adjusted.getRootNode())
      adjusted = root.host;
    event.target = adjusted;
    target._invoke(event, capture);
  };
  try {
    event.eventPhase = 1;
    for (const target of [...path].reverse()) {
      invoke(target, true);
      if (event.cancelBubble) break;
    }
    if (!event.cancelBubble) {
      event.eventPhase = 2;
      invoke(this, true);
      if (!event._immediate) invoke(this, false);
    }
    if (event.bubbles && !event.cancelBubble) {
      event.eventPhase = 3;
      for (const target of path) {
        invoke(target, false);
        if (event.cancelBubble) break;
      }
    }
    return !event.defaultPrevented;
  } finally {
    event.target = origin;
    event._path = [];
    event.eventPhase = 0;
    event.currentTarget = null;
    event._dispatching = false;
  }
}
function __dispatchInputEvent(target, event) {
  return __dispatchBrowserEvent.call(target, event, true);
}

Object.assign(
    globalThis,
    {Event, SubmitEvent, ProgressEvent, CustomEvent, PopStateEvent, HashChangeEvent, EventTarget});
const __windowTarget = new EventTarget();
globalThis.Window = class Window extends EventTarget {
  constructor() { throw new TypeError('Illegal constructor'); }
};
Object.setPrototypeOf(globalThis, Window.prototype);
__eventListenerStates.set(globalThis, __eventListeners(__windowTarget));
for (const name of ['addEventListener', 'removeEventListener'])
  globalThis[name] = __windowTarget[name].bind(globalThis);
globalThis._invoke = function(event, capture) {
  EventTarget.prototype._invoke.call(globalThis, event, capture);
};
globalThis.dispatchEvent = function(event) {
  return EventTarget.prototype.dispatchEvent.call(globalThis, event);
};
for (const name of ['Event', 'CustomEvent', 'EventTarget', 'Window'])
  Object.defineProperty(globalThis[name].prototype, Symbol.toStringTag, {
    value: name, configurable: true
  });
