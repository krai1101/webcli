// Original test transport, not a page polyfill. Upstream scripts are unchanged.
// The host QuickJS context is separate from each WebCLI document context.
class Page {
  goto(url) { return __call('goto', url); }
  fill(selector, value) { return __call('fill', selector, value); }
  click(selector) { return __call('click', selector); }
  selectOption(selector, value) { return __call('selectOption', selector, value); }
  setChecked(selector, checked) { return __call('setChecked', selector, String(checked)); }
  waitForSelector(selector) { return __call('waitForSelector', selector); }
  waitForState(state) { return __call('waitForState', state); }
  waitForScript(expression) { return __call('waitForScript', expression); }
  evaluate(expression) { return __call('evaluate', 'String((' + expression + '))'); }
  extract(schema) {
    // Implement only the schema operations used by the pinned upstream tests.
    // Selection, trimming, array limits and URL resolution retain their meaning.
    return __call('evaluate', '(' + function(schema) {
      function read(root, spec) {
        if (typeof spec === 'string') {
          const node = spec ? root.querySelector(spec) : root;
          return node ? node.textContent.trim() : null;
        }
        if (Array.isArray(spec)) {
          const rule = typeof spec[0] === 'string' ? {selector: spec[0]} : spec[0];
          const nodes = Array.from(root.querySelectorAll(rule.selector));
          return (typeof rule.limit === 'number' ? nodes.slice(0, rule.limit) : nodes).map(node=>value(node,rule));
        }
        const node = spec.selector ? root.querySelector(spec.selector) : root;
        return node ? value(node, spec) : null;
      }
      function value(node, rule) {
        if (rule.fields) return Object.fromEntries(Object.entries(rule.fields).map(([key, spec])=>[key,read(node,spec)]));
        if (!rule.attr) return node.textContent.trim();
        const attribute = node.getAttribute(rule.attr);
        return attribute !== null && ['href','src'].includes(rule.attr) ? new URL(attribute,document.baseURI).href : attribute;
      }
      const result = Object.fromEntries(Object.entries(schema).map(([key, spec])=>[key,read(document,spec)]));
      if (Object.values(result).every(value=>value===null)) throw new Error('extract: no schema selector matched');
      return result;
    } + ')(' + JSON.stringify(schema) + ')');
  }
}
