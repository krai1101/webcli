function __isControlDisabled(node) {
  if (node.disabled) return true;
  for (let parent = node.parentNode; parent; parent = parent.parentNode) {
    if (parent.tagName !== 'FIELDSET' || !parent.hasAttribute('disabled')) continue;
    const legend = parent.children.find(child => child.tagName === 'LEGEND');
    if (!legend || !legend.contains(node)) return true;
  }
  return false;
}

function __isReadOnlyControl(node) {
  return node.readOnly && (node.tagName === 'TEXTAREA' || node.tagName === 'INPUT' &&
    !['checkbox', 'radio', 'image', 'file', 'button', 'submit', 'reset', 'range', 'color',
      'hidden'
    ].includes(node.type));
}
