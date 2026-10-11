// Browser API promises use runtime intrinsics, independent of author polyfills.
const __nativePromise = Promise;
const __nativeResolve = Promise.resolve.bind(Promise);
const __nativeReject = Promise.reject.bind(Promise);
const __queueMicrotask = queueMicrotask;
const __parseURL = __webcliURLParse;
const __webcliBaseURI = () => __webcliCurrentURL;
globalThis.self = globalThis;
