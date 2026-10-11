// Namespace identities come from Lexbor, shared by parsed and created nodes.
const __htmlNamespace = 'http://www.w3.org/1999/xhtml';
const __svgNamespace = 'http://www.w3.org/2000/svg';
{
  const identities = new WeakMap();
  const identity = node => {
    if (!identities.has(node)) identities.set(node, __webcliElementIdentity(node.__id));
    return identities.get(node);
  };
  const originalName = Object.getOwnPropertyDescriptor(DomNode.prototype, 'nodeName').get;
  Object.defineProperties(DomNode.prototype, {
    namespaceURI: {
      get() {
        return this.nodeType === 1 ? identity(this).namespaceURI : null;
      }
    },
    prefix: {
      get() {
        return this.nodeType === 1 ? identity(this).prefix : null;
      }
    },
    localName: {
      configurable: true,
      get() {
        return this.nodeType === 1 ? identity(this).localName : undefined;
      }
    },
    nodeName: {
      configurable: true,
      get() {
        if (this.nodeType !== 1) return originalName.call(this);
        const name = identity(this).qualifiedName;
        return this.namespaceURI === __htmlNamespace ? name.toUpperCase() : name;
      }
    }
  });
  const namePattern =
      /^[A-Za-z_\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u02ff\u0370-\u037d\u037f-\u1fff\u200c-\u200d\u2070-\u218f\u2c00-\u2fef\u3001-\ud7ff\uf900-\ufdcf\ufdf0-\ufffd\u{10000}-\u{effff}][A-Za-z_0-9.\-\u00b7\u0300-\u036f\u203f-\u2040\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u02ff\u0370-\u037d\u037f-\u1fff\u200c-\u200d\u2070-\u218f\u2c00-\u2fef\u3001-\ud7ff\uf900-\ufdcf\ufdf0-\ufffd\u{10000}-\u{effff}]*$/u;
  const exception = (name, message) => new DOMException(message,name);
  const qualified = (namespace, name) => {
    const uri = namespace == null ? '' : String(namespace), parts = String(name).split(':');
    if (parts.length > 2 || parts.some(part => !namePattern.test(part)))
      throw exception('InvalidCharacterError', 'invalid qualified name');
    const prefix = parts.length === 2 ? parts[0] : '', local = parts[parts.length - 1];
    if (prefix && !uri || prefix === 'xml' && uri !== 'http://www.w3.org/XML/1998/namespace' ||
        (prefix === 'xmlns' || name === 'xmlns') && uri !== 'http://www.w3.org/2000/xmlns/' ||
        uri === 'http://www.w3.org/2000/xmlns/' && prefix !== 'xmlns' && name !== 'xmlns')
      throw exception('NamespaceError', 'qualified name does not match namespace');
    return {uri, prefix, local};
  };
  globalThis.SVGElement = class SVGElement extends Element {
    constructor() {
      throw new TypeError('Illegal SVGElement constructor');
    }
  };
  globalThis.__webcliInstallDocumentFactories = target => {
    target.createTextNode = function(text) {
      if (!arguments.length) throw new TypeError('text required');
      return __wrap(__webcliCreateText(String(text), target.__id));
    };
    target.createComment = function(text) {
      if (!arguments.length) throw new TypeError('comment text required');
      return __wrap(__webcliCreateComment(String(text), target.__id));
    };
    target.createDocumentFragment = () => __wrap(__webcliCreateFragment(target.__id));
    target.createRange = () => __newRange(target);
    target.createElement = function(name) {
      if (!arguments.length) throw new TypeError('element name required');
      name = String(name).replace(/[A-Z]/g, character => character.toLowerCase());
      if (!namePattern.test(name.replace(/:/g, '_')))
        throw exception('InvalidCharacterError', 'invalid element name');
      const node = __wrap(__webcliCreateElementNS(target.__id, __htmlNamespace, name, ''));
      if (typeof __upgradeComponent === 'function') __upgradeComponent(node);
      return node;
    };
    target.createElementNS = function(namespace, name) {
      if (arguments.length < 2) throw new TypeError('namespace and element name required');
      name = String(name);
      const {uri, prefix, local} = qualified(namespace, name);
      const node = __wrap(__webcliCreateElementNS(target.__id, uri, local, prefix));
      if (uri === __htmlNamespace && typeof __upgradeComponent === 'function')
        __upgradeComponent(node);
      return node;
    };
  };
  __webcliInstallDocumentFactories(document);
  DomNode.prototype.getAttributeNS = function(namespace, local) {
    return __webcliAttributeNS(
        this.__id, namespace == null ? '' : String(namespace), String(local));
  };
  DomNode.prototype.hasAttributeNS = function(namespace, local) {
    return this.getAttributeNS(namespace, local) !== null;
  };
  DomNode.prototype.setAttributeNS = function(namespace, name, value) {
    name = String(name);
    const {uri, local} = qualified(namespace, name), oldValue = this.getAttributeNS(uri, local);
    __webcliAttributeNS(this.__id, uri, local, name, String(value));
    __notify(this, 'attributes', {attributeName: local, attributeNamespace: uri || null, oldValue});
    if (this._ceState === 'custom' && this._ceDefinition.attributes.includes(local))
      __componentCall(
          this, 'attributeChangedCallback', local, oldValue, String(value), uri || null);
  };
  DomNode.prototype.removeAttributeNS = function(namespace, local) {
    const uri = namespace == null ? '' : String(namespace);
    local = String(local);
    const oldValue = this.getAttributeNS(uri, local);
    if (oldValue === null) return;
    __webcliAttributeNS(this.__id, uri, local, null);
    __notify(this, 'attributes', {attributeName: local, attributeNamespace: uri || null, oldValue});
    if (this._ceState === 'custom' && this._ceDefinition.attributes.includes(local))
      __componentCall(this, 'attributeChangedCallback', local, oldValue, null, uri || null);
  };
}
