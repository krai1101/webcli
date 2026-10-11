// Serialize the DOM without attaching clones or modifying the live document.
globalThis.XMLSerializer = class XMLSerializer {
  serializeToString(root) {
    if (!(root instanceof Node)) throw new TypeError('Node required');
    const escapeText = value => String(value).replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
    const escapeAttribute = value => escapeText(value).replace(/"/g, '&quot;')
        .replace(/\t/g, '&#9;').replace(/\n/g, '&#10;').replace(/\r/g, '&#13;');
    const xml = 'http://www.w3.org/XML/1998/namespace';
    const xmlns = 'http://www.w3.org/2000/xmlns/';
    const parts = [], stack = [{node: root, namespaces: new Map([['xml', xml], ['', '']])}];
    let generatedPrefix = 0;
    while (stack.length) {
      const entry = stack.pop();
      if (entry.end) { parts.push(entry.end); continue; }
      const {node} = entry;
      switch (node.nodeType) {
        case 1: {
          const namespaces = new Map(entry.namespaces), attributes = Array.from(node.attributes);
          const declarations = [];
          for (const attribute of attributes) {
            if (attribute.name === 'xmlns') namespaces.set('', attribute.value);
            else if (attribute.namespaceURI === xmlns || attribute.name.startsWith('xmlns:'))
              namespaces.set(attribute.localName, attribute.value);
          }
          const bind = (prefix, uri) => {
            if (namespaces.get(prefix) === uri) return;
            namespaces.set(prefix, uri);
            declarations.push((prefix ? 'xmlns:' + prefix : 'xmlns') + '="' +
                escapeAttribute(uri) + '"');
          };
          const uri = node.namespaceURI || '', prefix = node.prefix || '';
          const name = (prefix ? prefix + ':' : '') + node.localName;
          bind(prefix, uri);
          const serializedAttributes = [];
          for (const attribute of attributes) {
            let name = attribute.name;
            if (attribute.namespaceURI && attribute.namespaceURI !== xmlns) {
              let prefix = attribute.prefix || '';
              if (!prefix || namespaces.has(prefix) && namespaces.get(prefix) !== attribute.namespaceURI) {
                prefix = Array.from(namespaces).find(([key, value]) => key && value === attribute.namespaceURI)?.[0];
                if (!prefix) {
                  do { prefix = 'ns' + (++generatedPrefix); } while (namespaces.has(prefix));
                }
              }
              bind(prefix, attribute.namespaceURI);
              name = prefix + ':' + attribute.localName;
            }
            serializedAttributes.push(name + '="' + escapeAttribute(attribute.value) + '"');
          }
          const children = node.childNodes;
          const suffix = !children.length && uri !== __htmlNamespace ? '/>' : '>';
          parts.push('<' + name + [...declarations, ...serializedAttributes]
              .map(attribute => ' ' + attribute).join('') + suffix);
          if (suffix === '/>') break;
          // XML serialization gives HTML void elements an explicit XML closing form.
          if (!children.length && /^(area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr)$/.test(name) && uri === __htmlNamespace) {
            parts[parts.length - 1] = parts[parts.length - 1].slice(0, -1) + ' />';
            break;
          }
          stack.push({end: '</' + name + '>'});
          for (let i = children.length - 1; i >= 0; --i) stack.push({node: children[i], namespaces});
          break;
        }
        case 3: parts.push(escapeText(node.data)); break;
        case 4: parts.push('<![CDATA[' + node.data + ']]>'); break;
        case 7: parts.push('<?' + node.nodeName + ' ' + node.data + '?>'); break;
        case 8: parts.push('<!--' + node.data + '-->'); break;
        case 10: parts.push('<!DOCTYPE ' + node.nodeName + '>'); break;
        case 9:
        case 11: {
          const children = node.childNodes;
          for (let i = children.length - 1; i >= 0; --i)
            stack.push({node: children[i], namespaces: entry.namespaces});
          break;
        }
        case 2: break;
        default: throw new DOMException('Unsupported node type', 'InvalidStateError');
      }
    }
    return parts.join('');
  }
};
