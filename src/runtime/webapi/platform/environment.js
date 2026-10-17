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
    const optional = {architecture: __webcliArchitecture, bitness: __webcliBitness,
      model: '', platformVersion: '', wow64: false, formFactors: ['Desktop']};
    for (const hint of requested)
      if (Object.hasOwn(optional, hint)) result[hint] = optional[hint];
    return __identityResolve(result);
  }
});
Object.defineProperty(globalThis, 'isSecureContext', {
  value: __webcliSecureContext, enumerable: true, configurable: true
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
  onLine: true,
  cookieEnabled: true
};
function __consoleMessage(values) {
  const describe = value => value instanceof Error
    ? String(value) + '\n' + (value.stack || '') : String(value);
  if (!values.length) return '';
  if (typeof values[0] !== 'string') return values.map(describe).join(' ');
  let next = 1;
  const message = values[0].replace(/%[sdifoOc]/g, token => {
    if (next >= values.length) return token;
    const value = values[next++];
    if (token === '%c') return '';
    if (token === '%d' || token === '%i') return String(parseInt(value, 10));
    if (token === '%f') return String(parseFloat(value));
    return describe(value);
  });
  return [message, ...values.slice(next).map(describe)].join(' ');
}
globalThis.console = {
  log() {},
  info() {},
  debug() {},
  warn(...values) {
    __webcliReportError(__consoleMessage(values), 'warning');
  },
  error(...values) {
    __webcliReportError(__consoleMessage(values));
  }
};


// Reflected disabled remains distinct from effective disabledness.
