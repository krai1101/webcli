// Focus eligibility and modal containment share the same composed ancestry.
let __focusedNode = null;
function __setFocusTarget(node) {__focusedNode = node;}
Object.defineProperty(document, 'activeElement', {
  configurable:true,
  get() {
    if (!__focusedNode?.isConnected) __focusedNode = null;
    return __focusedNode || document.body || document.documentElement;
  }
});
function __focusParent(node) {
  return node.parentElement || node.getRootNode()?.host || null;
}
function __insideFocusScope(node, scope) {
  for (; node; node = __focusParent(node)) if (node === scope) return true;
  return false;
}
function __focusIndex(node) {
  const value = node.getAttribute('tabindex');
  if (value !== null && /^[+-]?\d+$/.test(value.trim())) return Number(value);
  if (node.isContentEditable || ['BUTTON', 'SELECT', 'TEXTAREA', 'IFRAME'].includes(node.tagName) ||
      node.tagName === 'INPUT' && node.type !== 'hidden' ||
      ['A', 'AREA'].includes(node.tagName) && node.hasAttribute('href')) return 0;
  return null;
}
function __canFocus(node, contain = true) {
  if (!(node instanceof Element) || !node.isConnected || __isControlDisabled(node)) return false;
  const modal = contain ? __topNativeDialog() : null;
  if (modal && !__insideFocusScope(node, modal)) return false;
  for (let parent = node; parent; parent = __focusParent(parent))
    if (parent.inert || getComputedStyle(parent).display === 'none') return false;
  if (['hidden', 'collapse'].includes(getComputedStyle(node).visibility)) return false;
  return __focusIndex(node) !== null || node.tagName === 'DIALOG' && node.open;
}
function __dialogFocusables(dialog, tabbable = true) {
  const result = [], pending = [...dialog.children].reverse();
  while (pending.length) {
    const node = pending.pop();
    const index = __focusIndex(node);
    if (__canFocus(node) && (!tabbable || index !== null && index >= 0)) result.push(node);
    const children = node.tagName === 'SLOT' && node.assignedElements().length ?
      node.assignedElements({flatten:true}) : node.shadowRoot?.children || node.children;
    pending.push(...[...children].reverse());
  }
  // Positive tabindex precedes the ordinary document-order tab sequence.
  return result.sort((a, b) => {
    const x = __focusIndex(a), y = __focusIndex(b);
    return (x > 0 ? x : Infinity) - (y > 0 ? y : Infinity);
  }).filter(node => !tabbable || node.tagName !== 'INPUT' || node.type !== 'radio' || !node.name ||
    !result.some(other => other !== node && other.tagName === 'INPUT' &&
      other.type === 'radio' && other.name === node.name && other.form === node.form && other.checked));
}
function __focusDialog(dialog) {
  const candidates = __dialogFocusables(dialog, false);
  (dialog.hasAttribute('autofocus') ? dialog :
    candidates.find(node => node.autofocus || node.hasAttribute('autofocus')) ||
    candidates[0] || dialog).focus();
}
function __tabInDialog(backward) {
  const dialog = __topNativeDialog();
  if (!dialog) return;
  const candidates = __dialogFocusables(dialog);
  if (!candidates.length) {dialog.focus(); return;}
  const index = candidates.indexOf(document.activeElement);
  const next = index < 0 ? (backward ? candidates.length - 1 : 0) :
    (index + (backward ? -1 : 1) + candidates.length) % candidates.length;
  candidates[next].focus();
}
