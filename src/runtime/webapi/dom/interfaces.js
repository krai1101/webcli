// WebIDL constants are exposed on both the constructor and its prototype.
for (const [name, value] of Object.entries({
    ELEMENT_NODE: 1,
    ATTRIBUTE_NODE: 2,
    TEXT_NODE: 3,
    CDATA_SECTION_NODE: 4,
    ENTITY_REFERENCE_NODE: 5,
    ENTITY_NODE: 6,
    PROCESSING_INSTRUCTION_NODE: 7,
    COMMENT_NODE: 8,
    DOCUMENT_NODE: 9,
    DOCUMENT_TYPE_NODE: 10,
    DOCUMENT_FRAGMENT_NODE: 11,
    NOTATION_NODE: 12,
    DOCUMENT_POSITION_DISCONNECTED: 1,
    DOCUMENT_POSITION_PRECEDING: 2,
    DOCUMENT_POSITION_FOLLOWING: 4,
    DOCUMENT_POSITION_CONTAINS: 8,
    DOCUMENT_POSITION_CONTAINED_BY: 16,
    DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32
  })) {
  Object.defineProperty(DomNode, name, {
    value,
    enumerable: true
  });
  Object.defineProperty(DomNode.prototype, name, {
    value,
    enumerable: true
  });
}

globalThis.Node = DomNode;
globalThis.Element = class Element extends DomNode {
  constructor() { throw new TypeError('Illegal Element constructor'); }
};
globalThis.HTMLElement = Element;
// React feature detection should use the native input-event path, not its old
// Internet Explorer propertychange/attachEvent fallback.
for (const name of ['input', 'change', 'click', 'focus', 'blur', 'keydown', 'keyup', 'keypress',
    'submit'
  ])
  Object.defineProperty(DomNode.prototype, 'on' + name, {
    value: null,
    writable: true,
    configurable: true
  });

function __setControlProperty(node, name, value) {
  Object.getOwnPropertyDescriptor(DomNode.prototype, name).set.call(node, value);
}
class HTMLIFrameElement extends Element {
  static[Symbol.hasInstance](node) {
    return node instanceof DomNode && node.tagName === 'IFRAME';
  }
}
globalThis.HTMLIFrameElement = HTMLIFrameElement;
