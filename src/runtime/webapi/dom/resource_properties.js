// Reflected attributes used by dynamically created resource elements.
// Native mutation notifications invalidate resource attachments and styles.
for (const [property, attribute] of [
         ['rel', 'rel'], ['media', 'media'], ['as', 'as'], ['integrity', 'integrity'],
         ['crossOrigin', 'crossorigin'], ['referrerPolicy', 'referrerpolicy'],
         ['fetchPriority', 'fetchpriority'], ['hreflang', 'hreflang']]) {
  Object.defineProperty(DomNode.prototype, property, {
    configurable: true,
    get() {
      return this.getAttribute(attribute) || '';
    },
    set(value) {
      this.setAttribute(attribute, String(value));
    }
  });
}
