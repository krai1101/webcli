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
  initUIEvent(type, bubbles = false, cancelable = false, view = null, detail = 0) {
    if (this._dispatching) return;
    if (view !== null && !(view instanceof Window)) throw new TypeError('Window required');
    this.initEvent(type, bubbles, cancelable);
    __uiEventState.set(this, {view, detail: Number(detail) | 0});
  }
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
  initMouseEvent(type, bubbles = false, cancelable = false, view = null, detail = 0,
      screenX = 0, screenY = 0, clientX = 0, clientY = 0, ctrlKey = false,
      altKey = false, shiftKey = false, metaKey = false, button = 0, relatedTarget = null) {
    if (this._dispatching) return;
    // Validate before changing the existing event; initialization resets trust.
    const fresh = new MouseEvent(type, {bubbles, cancelable, view, detail,
      screenX, screenY, clientX, clientY, ctrlKey, altKey, shiftKey, metaKey,
      button, relatedTarget});
    this.initUIEvent(type, bubbles, cancelable, view, detail);
    __mouseEventState.set(this, __mouseEventState.get(fresh));
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

globalThis.PointerEvent = class PointerEvent extends MouseEvent {
  constructor(type, options = {}) {
    options ??= {};
    super(type, options);
    for (const [name, fallback] of Object.entries({pointerId:0,width:1,height:1,
        pressure:0,tangentialPressure:0,tiltX:0,tiltY:0,twist:0}))
      Object.defineProperty(this, name, {value:Number(options[name] ?? fallback), enumerable:true});
    Object.defineProperties(this, {
      pointerType:{value:String(options.pointerType ?? ''), enumerable:true},
      isPrimary:{value:!!options.isPrimary, enumerable:true}
    });
  }
};
