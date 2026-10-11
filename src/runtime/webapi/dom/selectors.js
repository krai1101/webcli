// Resolve dynamic page-state pseudo-classes before passing selectors to Lexbor.
function __focusWithinSelector(root) {
  const tree = root?.nodeType === 9 ? root : root?.getRootNode();
  const owner = root?.nodeType === 9 ? root : root?.ownerDocument;
  let active = tree?.nodeType === 11 ? tree.activeElement : owner?.activeElement;
  if (!active || active.getRootNode() !== tree) return ':not(*)';
  const paths = [];
  for (let node = active; node; node = node.parentElement) {
    const parts = [];
    let current = node;
    while (current.parentElement) {
      parts.unshift(':nth-child(' + (current.parentElement.children.indexOf(current) + 1) + ')');
      current = current.parentElement;
    }
    if (tree.nodeType === 9) {
      paths.push([':root', ...parts].join(' > '));
    } else {
      parts.unshift(':nth-child(' + (tree.children.indexOf(current) + 1) + ')');
      paths.push(parts.join(' > '));
    }
  }
  return ':is(' + paths.join(',') + ')';
}

function __targetSelector(input, root) {
  const source = String(input);
  let output = '', quote = '', brackets = 0;
  let fragment = '';
  const owner = root?.nodeType === 9 ? root : root?.ownerDocument;
  const hash = !owner || owner === document ? location.hash : '';
  try {
    fragment = decodeURIComponent(hash.slice(1));
  } catch (_) {
    fragment = hash.slice(1);
  }
  const escaped =
      fragment.replace(/[\x00-\x1f\x7f"\\]/g, char => '\\' + char.charCodeAt(0).toString(16) + ' ');
  for (let i = 0; i < source.length; ++i) {
    const char = source[i];
    if (char === '\\') {
      output += char;
      if (i + 1 < source.length) output += source[++i];
      continue;
    }
    if (quote) {
      output += char;
      if (char === quote) quote = '';
      continue;
    }
    if (char === '"' || char === '\'') {
      quote = char;
      output += char;
      continue;
    }
    if (char === '[') ++brackets;
    if (char === ']') --brackets;
    if (!brackets && source.slice(i, i + 13).toLowerCase() === ':focus-within' &&
        !/[\w-]/.test(source[i + 13] || '')) {
      output += __focusWithinSelector(root);
      i += 12;
    } else if (!brackets && source.slice(i, i + 13).toLowerCase() === ':popover-open' &&
        !/[\w-]/.test(source[i + 13] || '')) {
      output += __popoverSelector(root);
      i += 12;
    } else if (!brackets && source.slice(i, i + 7).toLowerCase() === ':target' &&
        !/[\w-]/.test(source[i + 7] || '')) {
      output += fragment ? '[id="' + escaped + '"]' : ':not(*)';
      i += 6;
    } else
      output += char;
  }
  return output;
}
