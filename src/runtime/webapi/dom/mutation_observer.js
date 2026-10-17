const __observers = new Set();
let __mutationQueued = false;

function __notify(target, type, extra = {}, rangeChange = null) {
  if (type === 'attributes' && extra.attributeName?.toLowerCase() === 'form')
    __webcliFormOwner(target.__id, true);
  if (typeof __prunePopovers === 'function') __prunePopovers();
  if (typeof __updateRanges === 'function') __updateRanges(target, type, extra, rangeChange);
  if (type === 'childList')
    for (const node of extra.addedNodes || []) __webcliReferrerMeta(node.__id);
  else if (type === 'attributes' && target.tagName === 'META' &&
           ['name', 'content'].includes(extra.attributeName?.toLowerCase()))
    __webcliReferrerMeta(target.__id);
  __webcliStyleInvalidate();
  if (type === 'childList' && globalThis.__webcliRefreshFrameIndices) {
    const hasFrame = node => node.nodeType === 1 &&
        (node.tagName === 'IFRAME' || node.querySelector('iframe'));
    if ([...(extra.addedNodes || []), ...(extra.removedNodes || [])].some(hasFrame))
      globalThis.__webcliRefreshFrameIndices();
  }
  globalThis.__webcliScheduleIntersectionChecks?.();
  if (typeof __slotMutation === 'function') __slotMutation(target, type, extra);
  for (const observer of __observers)
    for (const entry of observer.targets) {
      if (entry.target !== target && !(entry.options.subtree && entry.target.contains(target)))
        continue;
      if (!entry.options[type]) continue;
      if (type === 'attributes' && entry.options.attributeFilter && !entry.options.attributeFilter
        .includes(extra.attributeName)) continue;
      const record = {
        type,
        target,
        addedNodes: [],
        removedNodes: [],
        previousSibling: null,
        nextSibling: null,
        attributeName: null,
        oldValue: null,
        ...extra
      };
      if (type === 'attributes' && !entry.options.attributeOldValue || type === 'characterData' && !
        entry.options.characterDataOldValue) record.oldValue = null;
      if (observer.records.length >= 1000) throw new Error(
        'MutationObserver record quota exceeded');
      observer.records.push(record);
      break;
    }
  if (!__mutationQueued) {
    __mutationQueued = true;
    __queueMicrotask(() => {
      __mutationQueued = false;
      const deliveries = [...__observers].map(observer => [observer, observer.takeRecords()]);
      for (const [observer, records] of deliveries)
        if (records.length) {
          try {
            observer.callback(records, observer);
          } catch (error) {
            __webcliReportError('MutationObserver: ' + error + (error?.stack ? '\n' + error
              .stack : ''));
          }
        }
    });
  }
}
class MutationObserver {
  constructor(callback) {
    if (typeof callback !== 'function') throw new TypeError('callback required');
    this.callback = callback;
    this.targets = [];
    this.records = [];
  }
  observe(target, options) {
    if (!(target instanceof DomNode)) throw new TypeError('observe requires a Node');
    options = {
      ...options
    };
    if (options.attributes === false && (options.attributeOldValue || options.attributeFilter) ||
      options.characterData === false && options.characterDataOldValue) throw new TypeError(
      'Contradictory mutation options');
    if (options.attributeOldValue || options.attributeFilter) options.attributes = options
      .attributes !== false;
    if (options.characterDataOldValue) options.characterData = options.characterData !== false;
    if (!options.childList && !options.attributes && !options.characterData) throw new TypeError(
      'observe requires mutation options');
    this.targets = this.targets.filter(x => x.target !== target);
    this.targets.push({
      target,
      options
    });
    __observers.add(this);
  }
  disconnect() {
    this.targets = [];
    this.records = [];
    __observers.delete(this);
  }
  takeRecords() {
    const records = this.records;
    this.records = [];
    return records;
  }
}
globalThis.MutationObserver = MutationObserver;
