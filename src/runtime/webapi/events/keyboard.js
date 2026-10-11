// Focused nonvisual keyboard events and basic activation defaults.
class KeyboardEvent extends Event {
  constructor(type, options = {}) {
    super(type, options);
    this.key = String(options.key || '');
    this.code = String(options.code || '');
    this.keyCode = Number(options.keyCode || 0);
    this.charCode = Number(options.charCode || 0);
    this.which = this.keyCode || this.charCode;
    this.ctrlKey = !!options.ctrlKey;
    this.altKey = !!options.altKey;
    this.shiftKey = !!options.shiftKey;
    this.metaKey = !!options.metaKey;
    this.repeat = !!options.repeat;
    this.isComposing = !!options.isComposing;
    this.location = Number(options.location || 0);
  }
  getModifierState(name) {
    return !!({
      Control: this.ctrlKey,
      Alt: this.altKey,
      Shift: this.shiftKey,
      Meta: this.metaKey
    } [name]);
  }
}
globalThis.KeyboardEvent = KeyboardEvent;

function __keyboard(node, name) {
  const codes = {
    Enter: 13,
    Escape: 27
  };
  if (!Object.hasOwn(codes, name)) throw new TypeError('unsupported keyboard key');
  node.focus();
  const send = type => node.dispatchEvent(new KeyboardEvent(type, {
    key: name,
    code: name,
    keyCode: codes[name],
    charCode: type === 'keypress' ? codes[name] : 0,
    bubbles: true,
    cancelable: true,
    composed: true
  }));
  const accepted = send('keydown') && (name !== 'Enter' || send('keypress'));
  if (accepted && name === 'Enter') {
    if (node.tagName === 'A' || node.tagName === 'BUTTON' ||
      node.tagName === 'INPUT' && ['submit', 'button', 'reset'].includes(node.type)) node.click();
    else if (node.tagName === 'INPUT' && node.form) {
      const types = ['text', 'search', 'email', 'url', 'tel', 'password', 'number', 'date', 'month',
        'week', 'time', 'datetime-local'
      ];
      if (types.includes(node.type)) {
        const form = node.form;
        const submit = form.elements.find(control => ['BUTTON', 'INPUT'].includes(control
          .tagName) && control.type === 'submit');
        if (submit) {
          if (!__isControlDisabled(submit)) submit.click();
        } else if (form.elements.filter(control => control.tagName === 'INPUT' && types.includes(
            control.type)).length === 1) form.requestSubmit();
      }
    }
  }
  send('keyup');
}
