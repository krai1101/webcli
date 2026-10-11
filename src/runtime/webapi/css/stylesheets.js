// Constructed stylesheet storage used by component libraries. No layout engine.
const __stylesheetMaxUnits = 1024 * 1024, __stylesheetMaxRules = 8192;
class CSSStyleSheet {
  constructor() {
    this._rules = [];
    this._disabled = false;
    this.media = {mediaText: ''};
    this.ownerNode = null;
    this._ownerSource = null;
    this._adopters = new Set();
  }
  get disabled() {
    return this.ownerNode ?
        __webcliStylesheetDisabled(this.ownerNode.__id) || this.ownerNode.hasAttribute('disabled') :
        this._disabled;
  }
  set disabled(value) {
    const previous = this._disabled;
    value = Boolean(value);
    if (this.ownerNode) __webcliStylesheetDisabled(this.ownerNode.__id, value);
    this._disabled = value;
    try {
      for (const owner of this._adopters) __syncAdoptedSheets(owner);
    } catch (error) {
      this._disabled = previous;
      for (const owner of this._adopters) __syncAdoptedSheets(owner);
      throw error;
    }
    globalThis.__webcliScheduleIntersectionChecks?.();
  }
  _refresh() {
    if (!this.ownerNode) return;
    const source = __webcliInlineStylesheet(this.ownerNode.__id);
    if (source === null) return;
    if (source === this._ownerSource) return;
    this._reading = true;
    try {
      this.replaceSync(source);
      this._ownerSource = source;
    } finally {
      this._reading = false;
    }
  }
  _commit(rules) {
    if (this.ownerNode && !this._reading) {
      const source = rules.map(rule => rule.cssText).join('\n');
      __webcliInlineStylesheet(this.ownerNode.__id, source);
      this._ownerSource = source;
    }
    this._rules = rules;
    for (const owner of this._adopters) __syncAdoptedSheets(owner);
  }
  get cssRules() {
    this._refresh();
    return this._rules;
  }
  get rules() {
    return this.cssRules;
  }
  replaceSync(text) {
    text = String(text);
    if (text.length > __stylesheetMaxUnits) throw new Error('stylesheet exceeds 1 Mi code units');
    const rules = [];
    let start = 0, depth = 0, parentheses = 0, quote = '', comment = false;
    for (let i = 0; i < text.length; ++i) {
      const char = text[i];
      if (comment) {
        if (char === '*' && text[i + 1] === '/') {
          comment = false;
          ++i;
        }
        continue;
      }
      if (quote) {
        if (char === '\\')
          ++i;
        else if (char === quote)
          quote = '';
        continue;
      }
      if (char === '/' && text[i + 1] === '*') {
        comment = true;
        ++i;
        continue;
      }
      if (char === '"' || char === '\'') {
        quote = char;
        continue;
      }
      if (char === '\\') {
        ++i;
        continue;
      }
      if (char === '(') ++parentheses;
      if (char === ')') parentheses = Math.max(0, parentheses - 1);
      if (parentheses) continue;
      if (char === '{') ++depth;
      if (char === '}') --depth;
      if (depth < 0) {
        depth = 0;
        start = i + 1;
        continue;
      }
      if ((char === '}' || char === ';') && depth === 0) {
        const cssText = text.slice(start, i + 1).trim();
        if (cssText) rules.push({cssText});
        start = i + 1;
        if (rules.length > __stylesheetMaxRules) throw new Error('stylesheet rule quota exceeded');
      }
    }
    // CSS parsing recovers at EOF; malformed trailing text does not reject the
    // entire sheet. Complete preceding rules remain available.
    this._commit(rules);
  }
  replace(text) {
    try {
      this.replaceSync(text);
      return Promise.resolve(this);
    } catch (error) {
      return Promise.reject(error);
    }
  }
  insertRule(text, index = 0) {
    this._refresh();
    if (!Number.isInteger(index) || index < 0 || index > this._rules.length)
      throw new RangeError('invalid rule index');
    const sheet = new CSSStyleSheet();
    sheet.replaceSync(text);
    if (sheet.cssRules.length !== 1) throw new SyntaxError('one rule required');
    if (this._rules.length >= __stylesheetMaxRules)
      throw new Error('stylesheet rule quota exceeded');
    if (this._rules.reduce((bytes, rule) => bytes + rule.cssText.length, 0) +
            sheet.cssRules[0].cssText.length >
        __stylesheetMaxUnits)
      throw new Error('stylesheet exceeds 1 Mi code units');
    const rules = this._rules.slice();
    rules.splice(index, 0, sheet.cssRules[0]);
    this._commit(rules);
    return index;
  }
  deleteRule(index) {
    this._refresh();
    if (!Number.isInteger(index) || index < 0 || index >= this._rules.length)
      throw new RangeError('invalid rule index');
    const rules = this._rules.slice();
    rules.splice(index, 1);
    this._commit(rules);
  }
}
globalThis.CSSStyleSheet = CSSStyleSheet;
// Inline CSSOM does not change DOM text. The visibility extractor also reads
// its native backing, so CSS-in-JS hide/show rules affect agent observations.
const __inlineSheets = new WeakMap(), __sheetLists = new WeakMap();
Object.defineProperty(DomNode.prototype, 'sheet', {
  get() {
    if (!['STYLE', 'LINK'].includes(this.tagName) || !this.isConnected) return null;
    if (this.tagName === 'LINK' && __webcliInlineStylesheet(this.__id) === null) return null;
    if (!__inlineSheets.has(this)) {
      const sheet = new CSSStyleSheet();
      sheet.ownerNode = this;
      __inlineSheets.set(this, sheet);
    }
    const sheet = __inlineSheets.get(this);
    sheet._refresh();
    return sheet;
  }
});
Object.defineProperty(Document.prototype, 'styleSheets', {
  get() {
    const owner = this;
    if (!__sheetLists.has(owner))
      __sheetLists.set(
          owner, new Proxy([], {
            get(_target, key) {
              const sheets =
                  owner.querySelectorAll('style,link').map(node => node.sheet).filter(Boolean);
              if (key === 'item') return index => sheets[Number(index)] ?? null;
              const value = Reflect.get(sheets, key);
              return typeof value === 'function' ? value.bind(sheets) : value;
            }
          }));
    return __sheetLists.get(owner);
  }
});
// Documents and shadow roots always expose an iterable collection.
const __adoptedSheets = new WeakMap();
function __syncAdoptedSheets(owner) {
  const source = (__adoptedSheets.get(owner) || [])
                     .filter(sheet => !sheet.disabled)
                     .map(sheet => sheet._rules.map(rule => rule.cssText).join('\n'))
                     .join('\n');
  __webcliAdoptedStylesheet(owner.__id, source);
  globalThis.__webcliScheduleIntersectionChecks?.();
}
for (const prototype of [Document.prototype, ShadowRoot.prototype]) {
  Object.defineProperty(prototype, 'adoptedStyleSheets', {
    configurable: true,
    get() {
      return (__adoptedSheets.get(this) || []).slice();
    },
    set(value) {
      const sheets = Array.from(value);
      if (sheets.length > 128 ||
          sheets.some(sheet => !(sheet instanceof CSSStyleSheet) || sheet.ownerNode))
        throw new TypeError('adoptedStyleSheets requires at most 128 constructed stylesheets');
      const previous = __adoptedSheets.get(this) || [];
      for (const sheet of previous) sheet._adopters.delete(this);
      __adoptedSheets.set(this, sheets);
      for (const sheet of sheets) sheet._adopters.add(this);
      try {
        __syncAdoptedSheets(this);
      } catch (error) {
        for (const sheet of sheets) sheet._adopters.delete(this);
        __adoptedSheets.set(this, previous);
        for (const sheet of previous) sheet._adopters.add(this);
        throw error;
      }
    }
  });
}
globalThis.CSS = {
  supports(property, value) {
      if (value === undefined && /^selector\(\s*:popover-open\s*\)$/.test(String(property)))
        return true;
    if (value === undefined) return false;
    return property === 'display' && /^(none|block|inline|inline-block|flex|grid)$/.test(value) ||
        property === 'visibility' && /^(visible|hidden|collapse)$/.test(value) ||
        property === 'opacity' && /^(?:\d+(?:\.\d+)?|\.\d+)$/.test(value);
  },
  escape(value) {
    const text = String(value);
    let result = '';
    for (let i = 0; i < text.length; ++i) {
      const code = text.charCodeAt(i), char = text[i];
      if (code === 0)
        result += '\ufffd';
      else if (
          code < 32 || code === 127 || i === 0 && /[0-9]/.test(char) ||
          i === 1 && text[0] === '-' && /[0-9]/.test(char))
        result += '\\' + code.toString(16) + ' ';
      else if (code >= 128 || /[a-zA-Z0-9_-]/.test(char) && !(text === '-' && i === 0))
        result += char;
      else
        result += '\\' + char;
    }
    return result;
  }
};
