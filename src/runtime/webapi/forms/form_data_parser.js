// Body packaging operates on bytes: file contents must never pass through a decoder.
const __parseFormData = (() => {
  const token = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;
  const invalid = () => new TypeError('Failed to parse body as FormData');

  function parameters(value) {
    const separator = value.indexOf(';');
    const essence = (separator < 0 ? value : value.slice(0, separator)).trim().toLowerCase();
    const result = new Map();
    let position = separator < 0 ? value.length : separator + 1;
    while (position < value.length) {
      while (/[\t ;]/.test(value[position] || '')) ++position;
      const start = position;
      while (position < value.length && value[position] !== '=' && value[position] !== ';')
        ++position;
      const name = value.slice(start, position).trim().toLowerCase();
      if (value[position] !== '=') {
        ++position;
        continue;
      }
      ++position;
      while (/[\t ]/.test(value[position] || '')) ++position;
      let content = '', valid = true;
      if (value[position] === '"') {
        ++position;
        let closed = false;
        while (position < value.length) {
          const character = value[position++];
          if (character === '"') {
            closed = true;
            break;
          }
          if (character === '\\' && position < value.length)
            content += value[position++];
          else
            content += character;
        }
        valid = closed;
        while (/[\t ]/.test(value[position] || '')) ++position;
        if (position < value.length && value[position] !== ';') valid = false;
        while (position < value.length && value[position] !== ';') ++position;
      } else {
        const begin = position;
        while (position < value.length && value[position] !== ';') ++position;
        content = value.slice(begin, position).trim();
        valid = content.length > 0;
      }
      if (valid && token.test(name) && !result.has(name)) result.set(name, content);
      ++position;
    }
    return {essence, values: result};
  }

  // KMP avoids quadratic scanning when a body repeats a boundary's prefix.
  function matcher(pattern) {
    const prefix = new Uint32Array(pattern.length);
    for (let i = 1, j = 0; i < pattern.length; ++i) {
      while (j && pattern[i] !== pattern[j]) j = prefix[j - 1];
      if (pattern[i] === pattern[j]) ++j;
      prefix[i] = j;
    }
    return (bytes, start) => {
      for (let i = start, j = 0; i < bytes.length; ++i) {
        while (j && bytes[i] !== pattern[j]) j = prefix[j - 1];
        if (bytes[i] === pattern[j]) ++j;
        if (j === pattern.length) return i + 1 - j;
      }
      return -1;
    };
  }

  function multipart(bytes, boundary) {
    if (!boundary || /[^\x20-\x7e]/.test(boundary)) throw invalid();
    // Even an empty multipart body needs its closing marker. Reject impossible
    // boundaries before allocating matcher tables from an untrusted header.
    if (boundary.length + 4 > bytes.length) throw invalid();
    const encode = value => new TextEncoder().encode(value);
    const marker = encode('--' + boundary);
    const findFirst = matcher(marker), findNext = matcher(encode('\r\n--' + boundary));
    const findHeaders = matcher(encode('\r\n\r\n'));
    const decode = value => new TextDecoder('utf-8', {ignoreBOM: true}).decode(value);
    const result = new FormData();
    const suffix = position => {
      const closing = bytes[position] === 45 && bytes[position + 1] === 45;
      if (closing) position += 2;
      while (bytes[position] === 32 || bytes[position] === 9) ++position;
      if (closing && position === bytes.length) return 'end';
      if (bytes[position] !== 13 || bytes[position + 1] !== 10) return null;
      return closing ? 'end' : position + 2;
    };
    let position = findFirst(bytes, 0), next;
    while (position >= 0) {
      next = suffix(position + marker.length);
      if ((position === 0 || (bytes[position - 2] === 13 && bytes[position - 1] === 10)) &&
          next !== null)
        break;
      position = findFirst(bytes, position + marker.length);
    }
    if (position < 0) throw invalid();
    while (next !== 'end') {
      const endHeaders = findHeaders(bytes, next);
      if (endHeaders < 0) throw invalid();
      const headers = new Map();
      for (const line of decode(bytes.subarray(next, endHeaders)).split('\r\n')) {
        const colon = line.indexOf(':');
        const name = line.slice(0, colon).toLowerCase();
        if (colon < 1 || !token.test(name)) throw invalid();
        if (!headers.has(name)) headers.set(name, line.slice(colon + 1).trim());
      }
      const disposition = parameters(headers.get('content-disposition') || '');
      if (disposition.essence !== 'form-data' || !disposition.values.has('name')) throw invalid();
      const startBody = endHeaders + 4;
      let endBody = findNext(bytes, startBody);
      while (endBody >= 0) {
        next = suffix(endBody + 2 + marker.length);
        if (next !== null) break;
        endBody = findNext(bytes, endBody + 2 + marker.length);
      }
      if (endBody < 0) throw invalid();
      const content = bytes.subarray(startBody, endBody);
      const name = disposition.values.get('name');
      if (disposition.values.has('filename')) {
        const type = headers.get('content-type') || 'text/plain';
        result.append(name, new File([content], disposition.values.get('filename'), {type}));
      } else {
        result.append(name, decode(content));
      }
    }
    return result;
  }

  function urlencoded(bytes) {
    const result = new FormData();
    const hex = byte => byte >= 48 && byte <= 57 ? byte - 48 :
        byte >= 65 && byte <= 70                 ? byte - 55 :
        byte >= 97 && byte <= 102                ? byte - 87 :
                                                   -1;
    const decode = (start, end) => {
      const copy = bytes.slice(start, end);
      let length = 0;
      for (let i = 0; i < copy.length; ++i) {
        const high = hex(copy[i + 1]), low = hex(copy[i + 2]);
        if (copy[i] === 37 && high >= 0 && low >= 0) {
          copy[length++] = high * 16 + low;
          i += 2;
        } else {
          copy[length++] = copy[i] === 43 ? 32 : copy[i];
        }
      }
      return new TextDecoder('utf-8', {ignoreBOM: true}).decode(copy.subarray(0, length));
    };
    for (let start = 0; start < bytes.length;) {
      let end = start, equals = -1;
      while (end < bytes.length && bytes[end] !== 38) {
        if (bytes[end] === 61 && equals < 0) equals = end;
        ++end;
      }
      if (end > start)
        result.append(
            decode(start, equals < 0 ? end : equals), decode(equals < 0 ? end : equals + 1, end));
      start = end + 1;
    }
    return result;
  }

  return (bytes, contentType) => {
    const type = parameters(contentType || '');
    if (type.essence === 'application/x-www-form-urlencoded') return urlencoded(bytes);
    if (type.essence === 'multipart/form-data')
      return multipart(bytes, type.values.get('boundary'));
    throw invalid();
  };
})();
