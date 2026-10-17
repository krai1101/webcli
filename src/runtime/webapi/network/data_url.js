function __dataURLResponse(request) {
  if (request.method !== 'GET') throw new TypeError('Data URL fetch requires GET');
  const url = request.url.split('#', 1)[0];
  const comma = url.indexOf(',');
  if (comma < 0) throw new TypeError('Malformed data URL');
  let type = url.slice(5, comma).trim(), source = url.slice(comma + 1);
  const base64 = /;base64$/i.test(type);
  if (base64) type = type.slice(0, -7).trimEnd();
  if (!type || type.startsWith(';')) type = 'text/plain' + (type || ';charset=US-ASCII');
  const encoded = new TextEncoder().encode(source);
  const decoded = new Uint8Array(encoded.length);
  const hex = byte => byte >= 48 && byte <= 57 ? byte - 48 :
      byte >= 65 && byte <= 70 ? byte - 55 : byte >= 97 && byte <= 102 ? byte - 87 : -1;
  let length = 0;
  for (let i = 0; i < encoded.length; ++i) {
    if (encoded[i] === 37 && hex(encoded[i + 1]) >= 0 && hex(encoded[i + 2]) >= 0) {
      decoded[length++] = hex(encoded[i + 1]) * 16 + hex(encoded[i + 2]); i += 2;
    } else decoded[length++] = encoded[i];
  }
  let bytes = decoded.subarray(0, length);
  if (base64) {
    let text = '';
    for (let i = 0; i < bytes.length; i += 4096)
      text += String.fromCharCode(...bytes.subarray(i, i + 4096));
    const binary = atob(text);
    bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
  }
  if (bytes.length > __webcliLimits.bufferedBodyBytes) throw new RangeError('Data URL body exceeds limit');
  let headers;
  try { headers = new Headers({'content-type': type}); }
  catch (_) { headers = new Headers({'content-type': 'text/plain;charset=US-ASCII'}); }
  return new Response(bytes, {url, type: 'basic', headers});
}
