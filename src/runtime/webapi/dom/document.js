globalThis.document = __wrap(0);
document.createRange = function() { return __newRange(this); };
Object.defineProperties(document, {
  readyState: {get: () => __webcliDocumentReadiness()},
  currentScript: {
    get: () => __wrap(__webcliCurrentScript)
  },
  URL: {
    get: () => location.href
  },
  domain: {
    get: () => location.hostname
  },
  location: {
    get: () => location,
    set: value => { globalThis.location = value; }
  },
  defaultView: {
    get: () => window
  },
  body: {
    get: () => document.querySelector('body')
  },
  head: {
    get: () => document.querySelector('head')
  },
  documentElement: {
    get: () => document.querySelector('html')
  },
  title: {
    get: () => document.head.querySelector('title')?.textContent || '',
    set(v) {
      let node = document.head.querySelector('title');
      if (!node) {
        node = document.createElement('title');
        document.head.appendChild(node);
      }
      node.textContent = v;
    }
  }
});
document.getElementById = function(id) {
  return document.querySelectorAll('[id]').find(n => n.id === String(id)) || null;
};
document.createElement = tag => __wrap(__webcliCreateElement(String(tag)));
document.createElementNS = (_ns, tag) => document.createElement(tag);
document.createTextNode = text => __wrap(__webcliCreateText(String(text)));
document.createDocumentFragment = () => __wrap(__webcliCreateFragment());
document.getElementsByTagName = tag => document.querySelectorAll(tag);
document.getElementsByClassName = value => document.querySelectorAll('.' + String(value).trim()
  .split(/\s+/).join('.'));
