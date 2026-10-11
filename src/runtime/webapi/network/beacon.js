// Queue delivery through the native transport. Beacon exposes queue acceptance,
// not a server response, and must not use page-defined fetch wrappers.
(() => {
  const transport = __webcliFetch, owner = navigator;
  const maximum = 64 * 1024;
  let pendingBytes = 0;
  Object.defineProperty(owner, 'sendBeacon', {
    configurable: true, enumerable: true, writable: true,
    value: function sendBeacon(url, data = null) {
      if (this !== owner) throw new TypeError('Navigator required');
      if (!arguments.length) throw new TypeError('Beacon URL required');
      const target = new URL(String(url), document.baseURI);
      if (!['http:', 'https:'].includes(target.protocol))
        throw new TypeError('Beacon requires an HTTP(S) URL');
      const body = __bodyInit(data);
      if (body.stream) throw new TypeError('Beacon does not accept streams');
      const bytes = body.bytes, size = bytes?.byteLength || 0;
      if (size > maximum - pendingBytes) return false;
      const essence = body.type.split(';')[0].trim().toLowerCase();
      const safe = !body.type || ['text/plain', 'application/x-www-form-urlencoded',
        'multipart/form-data'].includes(essence);
      let delivery;
      try {
        delivery = transport(target.href, {
          method: 'POST', body: bytes?.buffer ?? null,
          headers: body.type ? {'content-type': body.type} : {},
          mode: safe ? 'no-cors' : 'cors', credentials: 'include'
        });
      } catch (_) { return false; }
      pendingBytes += size;
      delivery.then(() => { pendingBytes -= size; }, () => { pendingBytes -= size; });
      return true;
    }
  });
})();
