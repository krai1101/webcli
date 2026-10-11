// MessageEvent is shared by same-window and inter-frame messaging. The frame
// bindings install postMessage delivery through document-owned task queues.
(() => {
  class MessageEvent extends Event {
    constructor(type, options = {}) {
      options ??= {};
      super(type, options);
      Object.defineProperties(this, {
        data: {value: 'data' in Object(options) ? options.data : null, enumerable: true},
        origin: {value: String(options.origin ?? ''), enumerable: true},
        lastEventId: {value: String(options.lastEventId ?? ''), enumerable: true},
        source: {value: options.source ?? null, enumerable: true},
        ports: {value: Object.freeze(Array.from(options.ports ?? [])), enumerable: true}
      });
    }
  }
  globalThis.MessageEvent = MessageEvent;
})();
