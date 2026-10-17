// Nonvisual outside activation supports explicit presentation backdrops and
// dialog wrappers with their own pointer/mouse-down handlers. There is no
// pixel hit testing: custom page handlers retain control over dismissal.
function __modalOutsideSurface(dialog) {
  const parent = dialog.parentElement || document.body;
  const backdrops = [...parent.children].filter(node => node !== dialog &&
    node.getAttribute('role') === 'presentation' &&
    node.getAttribute('aria-hidden') === 'true' && !node.textContent.trim() &&
    getComputedStyle(node).display !== 'none');
  if (backdrops.length) return backdrops.at(-1);
  // A wrapper may contain both the content and its outside-click surface.
  // Sending events to its parent skips these handlers entirely. Do not infer
  // dismissal from a class name or run multiple candidate clicks.
  const listeners = __eventListenerStates.get(dialog);
  if (listeners?.has('mousedown') || listeners?.has('pointerdown')) return dialog;
  return parent;
}

function __inputOutside(dialog) {
  if (!dialog.isConnected) throw new DOMException('Dialog was removed', 'InvalidStateError');
  const rect = dialog.getBoundingClientRect();
  const points = [{clientX:0,clientY:0},{clientX:innerWidth-1,clientY:0},
    {clientX:innerWidth-1,clientY:innerHeight-1},{clientX:0,clientY:innerHeight-1}];
  const position = points.find(({clientX:x,clientY:y}) =>
    x < rect.left || x > rect.right || y < rect.top || y > rect.bottom) || points[0];
  if (dialog.tagName === 'DIALOG') {
    __inputClick(dialog, position, {focus:false});
    if (dialog.open && dialog.closedBy === 'any') dialog.requestClose();
    return;
  }
  // Outside dialog is a virtual action, not a focusable page control. Preserve
  // focus while the page decides whether to dismiss and where to restore it.
  // Clearing focus first can trigger the focus trap and consume dismissal.
  __inputClick(__modalOutsideSurface(dialog), position, {focus:false});
}
