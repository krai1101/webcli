// Dynamically inserted classic scripts use the resource loader, independently
// of the fetch/XHR origin policy. Async scripts run on completion; async=false
// scripts start fetching immediately but execute in insertion order.
const __scriptResolve = Promise.resolve.bind(Promise);
const __scriptThen = Function.call.bind(Promise.prototype.then);
let __orderedScripts = __scriptResolve();

// The parser's preload scanner starts downloads without running their source.
// Both handlers attach immediately; failures belong to execution of that script.
function __preloadParserScript(node, url) {
  try {
    node._parserResource = __webcliLoadScript(url, node.__id).then(
      response => ({response}), error => ({error}));
  } catch (error) {
    node._parserResource = Promise.resolve({error});
  }
}
// External scripts inserted by author code delay the document load event too.
// Keep the blocker through ordered execution and resource-event delivery, not
// merely through downloading: load handlers may finish application startup.
globalThis.__webcliPendingScriptLoads = 0;
function __trackScriptLoad(completion) {
  ++__webcliPendingScriptLoads;
  return __scriptThen(__scriptResolve(completion), value => {
    --__webcliPendingScriptLoads;
    return value;
  }, error => {
    --__webcliPendingScriptLoads;
    throw error;
  });
}
function __startParserAsync(node) {
  node._parserPending = false;
  try {
    __dynamicScript(node);
  } catch (error) {
    __scriptFailure(node, error, node.src);
  }
}
function __scriptFailure(node, error, url) {
  __webcliReportError('script ' + url + ': ' + String(error) + (error?.stack ? '\n'+error.stack : ''));
  try { node.dispatchEvent(new Event('error')); }
  catch (handlerError) { __webcliReportError(handlerError?.stack || String(handlerError)); }
}
function __executeScript(node, source, url, external, module = false) {
  const previous = __webcliCurrentScript;
  let failure, completion;
  try {
    __webcliCurrentScript = module ? -1 : node.__id;
    completion=__webcliExecuteScript(source, url, module, node.__id);
  } catch (error) { failure = error; }
  finally { __webcliCurrentScript = previous; }
  if (failure) { __scriptFailure(node, failure, url); return; }
  if(module)return Promise.resolve(completion).then(()=>{
    if(external)node.dispatchEvent(new Event('load'));
  }).catch(error=>__scriptFailure(node,error,url));
  if (external) {
    try { node.dispatchEvent(new Event('load')); }
    catch (error) { __webcliReportError(error?.stack || String(error)); }
  }
}
function __dynamicScript(node) {
  if (globalThis.__webcliInitializingDOM || node._parserPending || node.tagName !== 'SCRIPT' || !node.isConnected || node.ownerDocument !== document || node._executed) return;
  const kind = __webcliScriptKind(node.getAttribute('type') || '');
  if (kind === 0) return;
  if (kind === 1 && node.hasAttribute('nomodule')) return;
  if (!node.getAttribute("src") && !node.textContent) return;
  node._executed = true;
  let url;
  try { url = node.src || location.href + '#dynamic-inline-script-'+node.__id; }
  catch (error) { __scriptFailure(node, error, node.getAttribute('src')); return; }
  if (!node.src) {
    const completion = __executeScript(node, node.textContent, url, false,kind===2);
    return kind === 2 ? __trackScriptLoad(completion) : completion;
  }
  let loaded;
  try {
    // Attach both handlers immediately, including for ordered scripts whose
    // predecessors have not completed, so network failures cannot go unhandled.
    loaded = node._parserResource || __webcliLoadScript(url, node.__id).then(
      response => ({response}), error => ({error}));
    node._parserResource = null;
  } catch (error) { __scriptFailure(node, error, url); return; }
  const finish = result => {
    if (result.error) __scriptFailure(node, result.error, url);
    else return __executeScript(node, result.response.body, result.response.url, true,kind===2);
  };
  if (node.async === false) {
    __orderedScripts = __orderedScripts.then(() => loaded).then(finish);
    return __trackScriptLoad(__orderedScripts);
  }
  return __trackScriptLoad(loaded.then(finish));
}

function __connectedScripts(root) {
  __dynamicScript(root);
  if (root.nodeType === 1) for (const script of root.querySelectorAll('script')) __dynamicScript(script);
}
