// Deterministic nonvisual browsing environment for responsive page logic.
Object.defineProperties(globalThis, {
  innerWidth: {value:__webcliViewport.width, configurable:true},
  innerHeight: {value:__webcliViewport.height, configurable:true},
  devicePixelRatio: {value:1, configurable:true}
});
globalThis.screen = {width:__webcliViewport.width,height:__webcliViewport.height,
  availWidth:__webcliViewport.width,availHeight:__webcliViewport.height,colorDepth:24,pixelDepth:24};
function __mediaMatches(query) {
  // CSS visibility and script-driven responsive layouts share one evaluator.
  return __webcliMediaMatches(String(query));
}
class MediaQueryList extends EventTarget {
  constructor(media) {super();this.media=String(media);this.onchange=null;}
  get matches() {return __mediaMatches(this.media);}
  addListener(callback) {this.addEventListener('change',callback);}
  removeListener(callback) {this.removeEventListener('change',callback);}
}
globalThis.MediaQueryList = MediaQueryList;
globalThis.matchMedia = query => new MediaQueryList(query);
