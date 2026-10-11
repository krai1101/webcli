const __uiEventState = new WeakMap();
const __mouseEventState = new WeakMap();
globalThis.UIEvent = class UIEvent extends Event {
  constructor(type, options = {}) {
    options ??= {};
    super(type, options);
    const view = options.view ?? null;
    if (view !== null && !(view instanceof Window)) throw new TypeError('Window required');
    __uiEventState.set(this, { view, detail: Number(options.detail ?? 0) | 0 });
  }
  get view() { return __uiEventState.get(this).view; }
  get detail() { return __uiEventState.get(this).detail; }
};
globalThis.MouseEvent = class MouseEvent extends UIEvent {
  constructor(type, options = {}) {
    options ??= {};
    super(type, options);
    const state = {};
    for (const name of ['screenX', 'screenY', 'clientX', 'clientY']) {
      state[name] = Number(options[name] ?? 0);
      if (!Number.isFinite(state[name])) throw new TypeError('Finite coordinate required');
    }
    for (const name of ['ctrlKey', 'shiftKey', 'altKey', 'metaKey']) state[name] = !!options[name];
    state.button = (Number(options.button ?? 0) << 16) >> 16;
    state.buttons = Number(options.buttons ?? 0) & 65535;
    state.relatedTarget = options.relatedTarget ?? null;
    if (state.relatedTarget !== null && !(state.relatedTarget instanceof EventTarget))
      throw new TypeError('EventTarget required');
    __mouseEventState.set(this, state);
  }
  getModifierState(key) {
    const name = { Alt: 'altKey', Control: 'ctrlKey', Meta: 'metaKey', Shift: 'shiftKey' }[String(key)];
    return name ? __mouseEventState.get(this)[name] : false;
  }
};
for (const name of ['screenX', 'screenY', 'clientX', 'clientY', 'ctrlKey', 'shiftKey',
    'altKey', 'metaKey', 'button', 'buttons', 'relatedTarget'])
  Object.defineProperty(MouseEvent.prototype, name, {
    get() { return __mouseEventState.get(this)[name]; }, configurable: true, enumerable: true
  });
for (const [name, source] of [['x', 'clientX'], ['y', 'clientY']])
  Object.defineProperty(MouseEvent.prototype, name, {
    get() { return this[source]; }, configurable: true, enumerable: true
  });
