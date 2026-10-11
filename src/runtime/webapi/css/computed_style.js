// Nonvisual style values for DOM logic. Geometry is deliberately unavailable.
const __blockTags = new Set('HTML BODY DIV P SECTION ARTICLE HEADER FOOTER MAIN NAV UL OL LI H1 H2 H3 H4 H5 H6 FORM PRE BLOCKQUOTE'.split(' '));
const __styleDefaults = Object.freeze({visibility: 'visible', opacity: '1', position: 'static',
  'box-sizing': 'content-box', overflow: 'visible', float: 'none', 'font-size': '16px',
  width: '0px', height: '0px', 'margin-top': '0px', 'margin-right': '0px',
  'margin-bottom': '0px', 'margin-left': '0px', 'padding-top': '0px', 'padding-right': '0px',
  'padding-bottom': '0px', 'padding-left': '0px', 'border-top-width': '0px',
  'border-right-width': '0px', 'border-bottom-width': '0px', 'border-left-width': '0px'});
const __computedStyles = new WeakMap();
globalThis.getComputedStyle = function(node) {
  if (!(node instanceof DomNode) || node.nodeType !== 1) throw new TypeError('getComputedStyle requires an element');
  if (__computedStyles.has(node)) return __computedStyles.get(node);
  const value = property => {
    if (property === 'display' || property === 'visibility') {
      const cascaded = __webcliComputedVisibility(node.__id, property === 'visibility');
      if (cascaded && !['inherit','initial','unset'].includes(cascaded)) return cascaded;
      if (cascaded === 'initial') return property === 'display' ? 'inline' : 'visible';
      if (cascaded === 'inherit' || cascaded === 'unset' && property === 'visibility')
        return node.parentElement ? getComputedStyle(node.parentElement)[property] :
          property === 'display' ? 'inline' : 'visible';
    }
    const inline = node.style.getPropertyValue(property);
    if (inline) return inline;
    if (property === 'display') return node.hidden || node.tagName === 'DIALOG' && !node.open || ['SCRIPT','STYLE','HEAD','TEMPLATE'].includes(node.tagName) ? 'none' :
      node.tagName === 'TABLE' ? 'table' : node.tagName === 'TR' ? 'table-row' :
      ['TD','TH'].includes(node.tagName) ? 'table-cell' : __blockTags.has(node.tagName) ? 'block' : 'inline';
    if (property === 'visibility' && node.parentElement) return getComputedStyle(node.parentElement).visibility;
    return __styleDefaults[property] ?? '';
  };
  const style = new Proxy({getPropertyValue: value, getPropertyPriority: () => '',
    item: index => node.style.item(index), get length() { return node.style.length; }}, {
    get(target, key) { return key in target ? Reflect.get(target,key) : typeof key === 'string' ?
      value(__cssPropertyName(key)) : undefined; },
    set() { throw new TypeError('computed style is read-only'); }
  });
  __computedStyles.set(node, style);
  return style;
};
