// Nonvisual frame batches use the existing bounded native timer queue.
{
  const callbacks = new Map();
  let next = 1, timer = null;
  const dispatch = () => {
    timer = null;
    const batch = [...callbacks.keys()], timestamp = performance.now();
    for (const id of batch) {
      const callback = callbacks.get(id);
      if (!callback) continue;
      callbacks.delete(id);
      try {callback(timestamp);} catch (error) {__webcliReportError(String(error)+(error?.stack ? '\n'+error.stack : ''));}
    }
  };
  globalThis.requestAnimationFrame = callback => {
    if (typeof callback !== 'function') throw new TypeError('frame callback required');
    if (callbacks.size >= 100) throw new Error('animation frame callback quota exceeded');
    const id = next++;
    callbacks.set(id,callback);
    if (timer === null) {
      try {timer = setTimeout(dispatch,16);} catch (error) {callbacks.delete(id);throw error;}
    }
    return id;
  };
  globalThis.cancelAnimationFrame = id => {
    callbacks.delete(Number(id));
    if (!callbacks.size && timer !== null) {clearTimeout(timer);timer=null;}
  };
}
