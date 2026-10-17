const __locationEngine = (() => {
  const navigate = (value, replace = false, reload = false, anchor = null) => {
    const oldURL = __webcliCurrentURL;
    const target = __webcliNavigate(String(value), document.baseURI);
    if (!reload && target.includes('#') &&
        target.split('#')[0] === oldURL.split('#')[0]) {
      if (target === oldURL) return;
      __historyEngine.commit(null, target, replace);
      const event = new HashChangeEvent('hashchange', {oldURL, newURL: target});
      schedule(() => {
        window.dispatchEvent(new PopStateEvent('popstate', {state: history.state}));
        window.dispatchEvent(event);
      }, 0);
    } else {
      __webcliQueueNavigation(target, replace, reload, anchor?.__id ?? -1);
    }
  };
  const schedule = setTimeout;
  const instance = {
    get href() {
      return __webcliCurrentURL;
    },
    set href(value) {
      navigate(value);
    },
    get origin() {
      return __parseURL(this.href).origin;
    },
    assign(value) {
      navigate(value);
    },
    replace(value) {
      navigate(value, true);
    },
    reload() {
      navigate(this.href, true, true);
    },
    toString() {
      return this.href;
    }
  };
  for (const property
           of ['protocol', 'host', 'hostname', 'port', 'pathname', 'search',
               'hash']) {
    const descriptor = {
      enumerable: true,
      configurable: false,
      get() {
        return __parseURL(this.href)[property];
      }
    };
    descriptor.set = function(value) {
      const url = __parseURL(this.href, undefined, property, String(value));
      if (property === 'hash' && url.href === this.href) return;
      navigate(url.href);
    };
    Object.defineProperty(instance, property, descriptor);
  }
  // Window.location is unforgeable and forwards assignment to href. Replacing
  // this binding would strand the old document after a successful redirect.
  Object.defineProperty(globalThis, 'location', {
    enumerable: true,
    configurable: false,
    get: () => instance,
    set: value => navigate(value)
  });
  return Object.freeze({followAnchor: anchor => navigate(anchor.href, false, false, anchor)});
})();
