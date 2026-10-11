// Detached native HTML documents share allocation quotas, not a page runtime.
globalThis.DOMParser = class DOMParser {
  parseFromString(source, type) {
    if (String(type) !== 'text/html') throw new TypeError('DOMParser supports text/html only');
    const lease = __webcliParseHTML(String(source));
    Object.defineProperty(lease, 'nodes', {value: new Map()});
    __parsedOwners.set(lease.rootId, new WeakRef(lease));
    const parsed = __wrap(lease.rootId);
    Object.setPrototypeOf(parsed, Document.prototype);
    Object.defineProperties(parsed, {
      body: {get: () => parsed.querySelector('body')},
      head: {get: () => parsed.querySelector('head')},
      documentElement: {get: () => parsed.querySelector('html')},
      title: {
        get: () => parsed.head.querySelector('title')?.textContent || '',
        set: value => {
          let title = parsed.head.querySelector('title');
          if (!title) { title = parsed.createElement('title'); parsed.head.appendChild(title); }
          title.textContent = value;
        }
      },
      defaultView: {value: null, configurable:true}, URL: {value: document.URL, configurable:true},
      contentType: {value: 'text/html'}, readyState: {value: 'complete'}
    });
    parsed.getElementById = id => parsed.querySelectorAll('[id]').find(node => node.id === String(id)) || null;
    __webcliInstallDocumentFactories(parsed);
    parsed.getElementsByTagName = tag => parsed.querySelectorAll(tag);
    parsed.getElementsByClassName = value => parsed.querySelectorAll('.' + String(value).trim().split(/\s+/).join('.'));
    return parsed;
  }
};
