// Weak registrations do not retain abandoned ranges or their boundary nodes.
function __updateRanges(target, type, record, change) {
  if (type !== 'childList' && type !== 'characterData') return;
  for (const ref of __liveRanges) {
    const range = ref.deref();
    if (!range) {
      __liveRanges.delete(ref);
      continue;
    }
    const state = __rangeState(range);
    for (const point of [state.start, state.end]) {
      if (type === 'characterData') {
        if (change?.split && change.parent) {
          if (point.node === target && point.offset > change.offset) {
            point.node = change.split;
            point.offset -= change.offset;
          } else if (point.node === change.parent && point.offset === change.index + 1)
            ++point.offset;
        } else if (point.node === target) {
          const offset = change?.offset ?? 0;
          const count = change?.count ?? record.oldValue.length;
          const length = change?.length ?? target.data.length;
          if (point.offset > offset && point.offset <= offset + count)
            point.offset = offset;
          else if (point.offset > offset + count)
            point.offset += length - count;
        }
        continue;
      }
      const removed = record.removedNodes || [], added = record.addedNodes || [];
      const index = change?.index ?? (added.length ? target.childNodes.indexOf(added[0]) : 0);
      for (const child of removed) {
        if (child.contains(point.node)) {
          point.node = target;
          point.offset = index;
        } else if (point.node === target && point.offset > index)
          --point.offset;
      }
      if (point.node === target && point.offset > index) point.offset += added.length;
    }
  }
}
