// Logical offsets only: this does not create a viewport or load feed content.
(() => {
  const schedule = setTimeout, dispatch = EventTarget.prototype.dispatchEvent.bind(globalThis);
  let x = 0, y = 0, queued = false;
  const coordinate = value => Number.isFinite(Number(value)) ? Number(value) : 0;
  const scroll = (first, second, relative) => {
    const options =
        first !== null && typeof first === 'object' ? first : {left: first, top: second};
    const nextX = options.left === undefined ? x : coordinate(options.left) + (relative ? x : 0);
    const nextY = options.top === undefined ? y : coordinate(options.top) + (relative ? y : 0);
    if (nextX === x && nextY === y) return;
    x = nextX;
    y = nextY;
    if (!queued) {
      queued = true;
      schedule(() => {
        queued = false;
        dispatch(new Event('scroll'));
      }, 0);
    }
  };
  for (const name of ['scrollX', 'pageXOffset', 'scrollY', 'pageYOffset'])
    Object.defineProperty(globalThis, name, {
      configurable: true,
      enumerable: true,
      get: () => name === 'scrollX' || name === 'pageXOffset' ? x : y
    });
  globalThis.scroll = globalThis.scrollTo = (first = {}, second) => scroll(first, second, false);
  globalThis.scrollBy = (first = {}, second) => scroll(first, second, true);
})();
