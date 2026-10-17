// Like the nonvisual geometry model, these APIs use estimated boxes, not CSS
// painting or a compositor. Geometry and hit testing deliberately agree.
(() => {
  const pointerTarget = element => {
    for (let node = element; node; node = node.parentElement) {
      const value = node.style.getPropertyValue('pointer-events');
      if (value) return value !== 'none';
    }
    return true;
  };
  document.elementFromPoint = function(x, y) {
    if (arguments.length < 2) throw new TypeError('Two coordinates required');
    x = Number(x); y = Number(y);
    if (!Number.isFinite(x) || !Number.isFinite(y)) throw new TypeError('Finite coordinates required');
    if (x < 0 || y < 0 || x >= innerWidth || y >= innerHeight) return null;
    const modal = __topNativeDialog();
    let match = null;
    for (const element of document.querySelectorAll('*')) {
      if (modal && !__insideFocusScope(element, modal) || !pointerTarget(element)) continue;
      const rects = element.getClientRects();
      if (!rects.length) continue;
      const rect = rects[0];
      if (rect.width > 0 && rect.height > 0 && x >= rect.left && x <= rect.right &&
          y >= rect.top && y <= rect.bottom) match = element;
    }
    return match || modal;
  };
  document.elementsFromPoint = function(x, y) {
    if (arguments.length < 2) throw new TypeError('Two coordinates required');
    const result = [];
    for (let element = document.elementFromPoint(x, y); element; element = element.parentElement)
      result.push(element);
    return result;
  };
})();
