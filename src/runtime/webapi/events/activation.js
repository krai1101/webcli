__installDomMembers(class {
  click() {
    __activateElement.call(this, false);
  }
  focus() {
    if (!__canFocus(this) || document.activeElement === this) return;
    const previous = document.activeElement;
    __setFocusTarget(this);
    if (previous && previous !== document.body) {
      __commitTextEdit(previous);
      __dispatchInputEvent(previous, new FocusEvent('blur', {composed:true, relatedTarget:this}));
      __dispatchInputEvent(previous, new FocusEvent('focusout', {
        bubbles: true,
        composed: true,
        relatedTarget:this
      }));
    }
    if (document.activeElement !== this) return;
    __dispatchInputEvent(this, new FocusEvent('focus', {composed:true, relatedTarget:previous}));
    __dispatchInputEvent(this, new FocusEvent('focusin', {
      bubbles: true,
      composed: true,
      relatedTarget:previous
    }));
  }
  blur() {
    if (document.activeElement !== this) return;
    __setFocusTarget(null);
    __commitTextEdit(this);
    __dispatchInputEvent(this, new FocusEvent('blur', {composed:true}));
    __dispatchInputEvent(this, new FocusEvent('focusout', {
      bubbles: true,
      composed: true
    }));
    const modal = __topNativeDialog();
    if (modal && __insideFocusScope(this, modal) &&
        !__insideFocusScope(document.activeElement, modal)) modal.focus();
  }
});

function __activateElement(trusted, position = {}) {
  if (__isControlDisabled(this) || __clickingNodes.has(this)) return;
  if (this.form && ['reset', 'image'].includes(this.type))
    throw new DOMException('Reset and image submit defaults are unsupported',
      'NotSupportedError');
  __clickingNodes.add(this);
  try {
    const toggle = this.tagName === 'INPUT' && (this.type === 'checkbox' || this.type ===
      'radio');
    const previous = this.checked;
    const radioGroup = toggle && this.type === 'radio' && this.name ?
      this.getRootNode().querySelectorAll('input').filter(node => node.type === 'radio' &&
        node.name === this.name && node.form === this.form).map(node => [node, node.checked]) :
      [];
    if (toggle) __setControlProperty(this, 'checked', this.type === 'radio' || !previous);
    const clickEvent = new PointerEvent('click', {
        ...position,
        bubbles: true,
        cancelable: true,
        composed: true,
        view: window,
        detail: trusted ? 1 : 0,
        pointerId: trusted ? 1 : -1,
        pointerType: trusted ? 'mouse' : '',
        isPrimary: trusted
      });
    if (!(trusted ? __dispatchInputEvent(this, clickEvent) : this.dispatchEvent(clickEvent))) {
      if (toggle) __setControlProperty(this, 'checked', previous);
      for (const [node, checked] of radioGroup) __setControlProperty(node, 'checked', checked);
      return;
    }
    if (toggle) {
      this.dispatchEvent(new Event('input', {
        bubbles: true,
        composed: true
      }));
      this.dispatchEvent(new Event('change', {
        bubbles: true
      }));
    }
    if (this.tagName === 'A' && this.href) __locationEngine.followAnchor(this);
    else if ((this.tagName === 'BUTTON' || this.tagName === 'INPUT') && this.type ===
      'submit' && this.form) this.form.requestSubmit(this);
    else if (typeof __activatePopover === 'function') __activatePopover(this);
  } finally {
    __clickingNodes.delete(this);
  }
}
// Follow the mousedown focus default: use the nearest focusable ancestor,
// otherwise clear focus. A decorative backdrop is not a focusable control.
function __focusForInput(node) {
  for (let target = node; target; target = target.parentElement) {
    if (__isControlDisabled(target)) continue;
    const tabIndex = target.getAttribute('tabindex');
    if (tabIndex !== null && /^[+-]?\d+$/.test(tabIndex.trim()) ||
        target.isContentEditable ||
        ['BUTTON', 'SELECT', 'TEXTAREA', 'IFRAME'].includes(target.tagName) ||
        target.tagName === 'INPUT' && target.type !== 'hidden' ||
        ['A', 'AREA'].includes(target.tagName) && target.hasAttribute('href')) {
      target.focus();
      return;
    }
  }
  document.activeElement?.blur();
}

function __inputClick(node, position = null, {focus = true} = {}) {
  if (__isControlDisabled(node)) {
    __webcliTraceEvent('blocked-disabled', node.__id, 0, false);
    return;
  }
  if (node.closest('[inert]')) {
    __webcliTraceEvent('blocked-inert', node.__id, 0, false);
    return;
  }
  const modal = __topNativeDialog();
  if (modal && !__insideFocusScope(node, modal)) {
    __webcliTraceEvent('blocked-inert', node.__id, 0, false);
    return;
  }
  const rect = node.getBoundingClientRect();
  position ??= {clientX:rect.left+rect.width/2,clientY:rect.top+rect.height/2};
  const options = {...position,bubbles: true, cancelable: true, composed: true, view: window, detail: 1,
    pointerId:1,pointerType:'mouse',isPrimary:true};
  const mouse = __dispatchInputEvent(node, new PointerEvent('pointerdown', {...options, detail:0,buttons:1,pressure:0.5}));
  if (mouse && __dispatchInputEvent(node, new MouseEvent('mousedown', {...options, buttons: 1})) && focus)
    __focusForInput(node);
  __dispatchInputEvent(node, new PointerEvent('pointerup', {...options,detail:0}));
  if (mouse) __dispatchInputEvent(node, new MouseEvent('mouseup', options));
  __activateElement.call(node, true, position);
}
