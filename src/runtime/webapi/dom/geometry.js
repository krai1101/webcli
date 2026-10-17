// Nonvisual geometry estimates. These keep measurement-based page logic
// running without claiming pixel layout, transforms, clipping or rendering.
(() => {
  const states = new WeakMap();
  const state = value => {
    const result = states.get(value);
    if (!result) throw new TypeError('DOMRect required');
    return result;
  };
  class DOMRectReadOnly {
    constructor(x = 0, y = 0, width = 0, height = 0) {
      states.set(this, {x: Number(x), y: Number(y), width: Number(width), height: Number(height)});
    }
    get x() {
      return state(this).x;
    }
    get y() {
      return state(this).y;
    }
    get width() {
      return state(this).width;
    }
    get height() {
      return state(this).height;
    }
    get top() {
      const r = state(this);
      return Math.min(r.y, r.y + r.height);
    }
    get right() {
      const r = state(this);
      return Math.max(r.x, r.x + r.width);
    }
    get bottom() {
      const r = state(this);
      return Math.max(r.y, r.y + r.height);
    }
    get left() {
      const r = state(this);
      return Math.min(r.x, r.x + r.width);
    }
    toJSON() {
      return {
        x: this.x,
        y: this.y,
        width: this.width,
        height: this.height,
        top: this.top,
        right: this.right,
        bottom: this.bottom,
        left: this.left
      };
    }
    static fromRect(rect = {}) {
      return new DOMRectReadOnly(rect.x, rect.y, rect.width, rect.height);
    }
    get[Symbol.toStringTag]() {
      return 'DOMRectReadOnly';
    }
  }
  class DOMRect extends DOMRectReadOnly {
    static fromRect(rect = {}) {
      return new DOMRect(rect.x, rect.y, rect.width, rect.height);
    }
    get[Symbol.toStringTag]() {
      return 'DOMRect';
    }
  }
  for (const name of ['x', 'y', 'width', 'height'])
    Object.defineProperty(DOMRect.prototype, name, {
      configurable: true,
      enumerable: true,
      get() {
        return state(this)[name];
      },
      set(value) {
        state(this)[name] = Number(value);
      }
    });
  globalThis.DOMRectReadOnly = DOMRectReadOnly;
  globalThis.DOMRect = DOMRect;
  const style = getComputedStyle;
  const visible = element => {
    if (!element.isConnected) return false;
    for (let node = element; node; node = node.parentElement || node.getRootNode()?.host) {
      const computed = style(node);
      if (computed.display === 'none') return false;
    }
    return !['hidden', 'collapse'].includes(style(element).visibility);
  };
  let generation = -1;
  let roots = new WeakMap();
  const position = element => {
    const revision = __webcliDOMRevision();
    if (revision !== generation) {generation = revision; roots = new WeakMap();}
    const root = element.getRootNode();
    let positions = roots.get(root);
    if (!positions) {
      positions = new WeakMap();
      let index = 0, node = root;
      while (node) {
        if (node.nodeType === 1) positions.set(node, index * 5);
        ++index;
        if (node.firstChild) {node = node.firstChild; continue;}
        while (node !== root && !node.nextSibling) node = node.parentNode;
        node = node === root ? null : node.nextSibling;
      }
      roots.set(root, positions);
    }
    return positions.get(element) ?? 0;
  };
  const axisSize = (element, axis) => {
    // An explicit inline size wins, including zero. Unsupported lengths are
    // left to the estimate, never treated as real computed pixel values.
    const inline = element.style.getPropertyValue(axis).trim();
    if (/^(?:\d+(?:\.\d*)?|\.\d+)(?:px)?$/.test(inline)) return parseFloat(inline);
    if (['IFRAME', 'IMG'].includes(element.tagName)) {
      const attribute = element.getAttribute(axis)?.trim() ?? '';
      if (/^(?:\d+(?:\.\d*)?|\.\d+)$/.test(attribute)) return Number(attribute);
    }
    const owner = element.ownerDocument;
    if (element === owner.documentElement || element === owner.body)
      return __webcliViewport[axis];
    // Lightpanda likewise uses a small nonzero box for unsized elements.
    return 5;
  };
  for (const [name, axis] of [['clientWidth', 'width'], ['clientHeight', 'height'],
      ['offsetWidth', 'width'], ['offsetHeight', 'height']])
    Object.defineProperty(Element.prototype, name, {
      configurable: true, enumerable: true,
      get() {
        if (!(this instanceof Element)) throw new TypeError('Element required');
        return visible(this) ? Math.round(axisSize(this, axis)) : 0;
      }
    });
  Element.prototype.getBoundingClientRect = function() {
    if (!(this instanceof Element)) throw new TypeError('Element required');
    return visible(this) ? new DOMRect(0, position(this), axisSize(this, 'width'), axisSize(this, 'height')) : new DOMRect();
  };
  Element.prototype.getClientRects = function() {
    if (!(this instanceof Element)) throw new TypeError('Element required');
    const rects = visible(this) ? [this.getBoundingClientRect()] : [];
    Object.defineProperty(rects, 'item', {value: index => rects[Number(index)] ?? null});
    return rects;
  };
})();
