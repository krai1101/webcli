// Browser API promises use runtime intrinsics, independent of author polyfills.
const __nativePromise = Promise;
const __nativeResolve = Promise.resolve.bind(Promise);
const __nativeReject = Promise.reject.bind(Promise);
const __queueMicrotask = queueMicrotask;
// Browser internals parse URLs independently of page URL constructors/polyfills.
const __parseURL = __webcliURLParse;
const __webcliBaseURI = () => document.baseURI;
"use strict";
// WebCLI's supported nonvisual Web API subset. Evaluated once per live document.
globalThis.window = globalThis;
globalThis.self = globalThis;
// Initial aliases are replaced by the frame tree bindings during bootstrap.
globalThis.parent = globalThis;
globalThis.top = globalThis;
globalThis.onhashchange = null;
globalThis.__webcliCurrentScript = -1;
globalThis.__webcliSubmission = null;
