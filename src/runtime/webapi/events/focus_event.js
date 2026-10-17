const __focusEventTargets = new WeakMap();
globalThis.FocusEvent = class FocusEvent extends UIEvent {
  constructor(type, options = {}) {
    options ??= {};
    super(type, options);
    const related = options.relatedTarget ?? null;
    if (related !== null && !(related instanceof EventTarget))
      throw new TypeError('EventTarget required');
    __focusEventTargets.set(this, related);
  }
  get relatedTarget() {
    if (!__focusEventTargets.has(this)) throw new TypeError('FocusEvent required');
    return __focusEventTargets.get(this);
  }
};
