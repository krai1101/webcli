// Tree relations follow the ordinary DOM tree, not the composed/shadow tree.
__installDomMembers(class {
  hasChildNodes() {
    return this.firstChild !== null;
  }
  compareDocumentPosition(other) {
    if (!(other instanceof DomNode)) throw new TypeError('Node required');
    if (this === other) return 0;
    const path = node => {
      const result = [];
      for (; node; node = node.parentNode) result.push(node);
      return result.reverse();
    };
    const left = path(this),
      right = path(other);
    if (left[0] !== right[0]) {
      // Ordering of disconnected roots is implementation-specific but stable.
      return Node.DOCUMENT_POSITION_DISCONNECTED |
        Node.DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC |
        (left[0].__id < right[0].__id ? Node.DOCUMENT_POSITION_FOLLOWING :
          Node.DOCUMENT_POSITION_PRECEDING);
    }
    let index = 0;
    while (index < left.length && index < right.length && left[index] === right[index]) ++
      index;
    if (index === left.length)
      return Node.DOCUMENT_POSITION_FOLLOWING | Node.DOCUMENT_POSITION_CONTAINED_BY;
    if (index === right.length)
      return Node.DOCUMENT_POSITION_PRECEDING | Node.DOCUMENT_POSITION_CONTAINS;
    for (let node = left[index].nextSibling; node; node = node.nextSibling)
      if (node === right[index]) return Node.DOCUMENT_POSITION_FOLLOWING;
    return Node.DOCUMENT_POSITION_PRECEDING;
  }
});
