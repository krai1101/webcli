// No layout boxes exist in WebCLI. Rectangles report zero geometry rather
// than inventing CSS dimensions, while allowing nonvisual code to continue.
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
  // Viewport dimensions are available; element layout boxes remain zero.
  for (const [name, axis] of [['clientWidth', 'width'], ['clientHeight', 'height']])
    Object.defineProperty(Element.prototype, name, {
      configurable: true, enumerable: true,
      get() {
        if (!(this instanceof Element)) throw new TypeError('Element required');
        const owner = this.ownerDocument;
        return owner?.defaultView && this === owner.documentElement ? __webcliViewport[axis] : 0;
      }
    });
  Element.prototype.getBoundingClientRect = function() {
    if (!(this instanceof Element)) throw new TypeError('Element required');
    return new DOMRect();
  };
  Element.prototype.getClientRects = function() {
    if (!(this instanceof Element)) throw new TypeError('Element required');
    const rects = [];
    Object.defineProperty(rects, 'item', {value: index => rects[Number(index)] ?? null});
    return rects;
  };
})();
