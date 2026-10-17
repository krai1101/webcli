// Weak entries preserve opening order without retaining detached dialog trees.
const __nativeDialogFocus = [];
function __topNativeDialog() {
  for (let index = __nativeDialogFocus.length - 1; index >= 0; --index) {
    const dialog = __nativeDialogFocus[index].dialog.deref();
    if (dialog?.isConnected && dialog.open && dialog._dialogModal) continue;
    __nativeDialogFocus.splice(index, 1);
    if (dialog) {dialog._dialogModal = false; __webcliDialogState(dialog.__id, false);}
  }
  return __nativeDialogFocus.at(-1)?.dialog.deref() || null;
}
function __releaseDialogFocus(dialog) {
  const wasTop = __nativeDialogFocus.at(-1)?.dialog.deref() === dialog;
  const index = __nativeDialogFocus.findIndex(entry => entry.dialog.deref() === dialog);
  if (index < 0) {
    const previous = dialog._dialogPreviousFocus?.deref();
    delete dialog._dialogPreviousFocus;
    if (!__insideFocusScope(document.activeElement, dialog)) return;
    if (previous && __canFocus(previous)) previous.focus();
    else document.activeElement.blur();
    return;
  }
  const previous = __nativeDialogFocus.splice(index, 1)[0].previous?.deref();
  if (!wasTop) return;
  if (previous && __canFocus(previous)) previous.focus();
  else {
    const modal = __topNativeDialog();
    if (modal) __focusDialog(modal);
    else if (__insideFocusScope(document.activeElement, dialog)) document.activeElement.blur();
  }
}
class HTMLDialogElement extends Element {
  constructor() {return document.createElement('dialog');}
  static [Symbol.hasInstance](node) {return node instanceof DomNode && node.tagName === 'DIALOG';}
  get open() {return this.hasAttribute('open');}
  set open(value) {
    this.toggleAttribute('open', !!value);
    if (!value) {
      this._dialogModal = false;
      __webcliDialogState(this.__id, false);
      __releaseDialogFocus(this);
    }
  }
  get returnValue() {return this._dialogReturnValue || '';}
  set returnValue(value) {this._dialogReturnValue = String(value);}
  get closedBy() {
    const value = this.getAttribute('closedby')?.toLowerCase();
    return ['any', 'closerequest', 'none'].includes(value) ? value :
      this._dialogModal ? 'closerequest' : 'none';
  }
  set closedBy(value) {this.setAttribute('closedby', value);}
  show() {
    if (this.open) {
      if (this._dialogModal) throw new DOMException('Dialog is already modal', 'InvalidStateError');
      return;
    }
    this._dialogModal = false;
    __webcliDialogState(this.__id, false);
    const previous = document.activeElement;
    this._dialogPreviousFocus = previous ? new WeakRef(previous) : null;
    this.open = true;
    __focusDialog(this);
  }
  showModal() {
    if (!this.isConnected || this.open && !this._dialogModal)
      throw new DOMException('Dialog must be connected and not nonmodal', 'InvalidStateError');
    if (this.open) return;
    const previous = document.activeElement;
    this._dialogModal = true;
    this.open = true;
    __webcliDialogState(this.__id, true);
    __nativeDialogFocus.push({dialog:new WeakRef(this), previous:previous ? new WeakRef(previous) : null});
    __focusDialog(this);
  }
  close(value) {
    if (!this.open) return;
    if (value !== undefined) this.returnValue = value;
    this.open = false;
    queueMicrotask(() => this.dispatchEvent(new Event('close')));
  }
  requestClose(value) {
    if (this.open && this.dispatchEvent(new Event('cancel', {cancelable:true}))) this.close(value);
  }
}
globalThis.HTMLDialogElement = HTMLDialogElement;
for (const node of __cachedNodes())
  if (node.tagName === 'DIALOG') Object.setPrototypeOf(node, HTMLDialogElement.prototype);
