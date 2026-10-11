__installDomMembers(class {
  click() {
    __activateElement.call(this, false);
  }
  focus() {
    if (__isControlDisabled(this) || document.activeElement === this) return;
    const previous = document.activeElement;
    document.activeElement = this;
    if (previous && previous !== document.body) {
      previous.dispatchEvent(new Event('blur'));
      previous.dispatchEvent(new Event('focusout', {
        bubbles: true,
        composed: true
      }));
    }
    this.dispatchEvent(new Event('focus'));
    this.dispatchEvent(new Event('focusin', {
      bubbles: true,
      composed: true
    }));
  }
  blur() {
    if (document.activeElement !== this) return;
    document.activeElement = document.body;
    this.dispatchEvent(new Event('blur'));
    this.dispatchEvent(new Event('focusout', {
      bubbles: true,
      composed: true
    }));
  }
});

function __activateElement(trusted) {
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
    const clickEvent = new MouseEvent('click', {
        bubbles: true,
        cancelable: true,
        composed: true,
        view: window,
        detail: trusted ? 1 : 0
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
function __inputClick(node) {
  if (__isControlDisabled(node) || node.closest('[inert]')) return;
  const options = {bubbles: true, cancelable: true, composed: true, view: window, detail: 1};
  if (__dispatchInputEvent(node, new MouseEvent('mousedown', {...options, buttons: 1})))
    node.focus();
  __dispatchInputEvent(node, new MouseEvent('mouseup', options));
  __activateElement.call(node, true);
}
