// page-defined Promise constructors, prototypes and species hooks.
const __identityResolve = Promise.resolve.bind(Promise);
const __identityBrands = Object.freeze([
  Object.freeze({brand: __webcliBrowserBrand, version: __webcliBrowserMajorVersion})
]);
const __identityFullVersions = Object.freeze([
  Object.freeze({brand: __webcliBrowserBrand, version: __webcliBrowserVersion})
]);
const __identityVersion = __webcliBrowserVersion;
const __identityPlatform = __webcliPlatform === 'Win32' ? 'Windows' :
  __webcliPlatform === 'MacIntel' ? 'macOS' :
  __webcliPlatform.startsWith('Linux') ? 'Linux' : 'Unknown';
const __identityData = Object.freeze({
  brands: __identityBrands,
  mobile: false,
  platform: __identityPlatform,
  toJSON() {
    return {brands: __identityBrands, mobile: false, platform: __identityPlatform};
  },
  getHighEntropyValues(hints) {
    const requested = Array.from(hints, String);
    const result = this.toJSON();
    if (requested.includes('fullVersionList')) result.fullVersionList = __identityFullVersions;
    if (requested.includes('uaFullVersion')) result.uaFullVersion = __identityVersion;
    return __identityResolve(result);
  }
});
let __windowName = '';
Object.defineProperty(globalThis, 'name', {
  configurable: true, enumerable: true,
  get: () => __windowName,
  set: value => { __windowName = String(value); }
});
globalThis.navigator = {
  userAgent: __webcliUserAgent,
  appVersion: __webcliUserAgent.slice(__webcliUserAgent.indexOf('/') + 1),
  platform: __webcliPlatform,
  language: 'en-US',
  languages: ['en-US', 'en'],
  userAgentData: __identityData,
  onLine: true
};
globalThis.console = {
  log() {},
  info() {},
  debug() {},
  warn(...values) {
    __webcliReportError(values.map(value => value instanceof Error ? String(value) + '\n' + (value
      .stack || '') : String(value)).join(' '), 'warning');
  },
  error(...values) {
    __webcliReportError(values.map(value => value instanceof Error ? String(value) + '\n' + (value
      .stack || '') : String(value)).join(' '));
  }
};


// Reflected disabled remains distinct from effective disabledness.
