// Deterministic nonvisual browsing environment for responsive page logic.
Object.defineProperties(globalThis, {
  innerWidth: {value:__webcliViewport.width, configurable:true},
  innerHeight: {value:__webcliViewport.height, configurable:true},
  devicePixelRatio: {value:1, configurable:true}
});
globalThis.screen = {width:__webcliViewport.width,height:__webcliViewport.height,
  availWidth:__webcliViewport.width,availHeight:__webcliViewport.height,colorDepth:24,pixelDepth:24};
function __mediaMatches(query) {
  return String(query).toLowerCase().split(',').some(part => {
    part = part.trim(); const negative = part.startsWith('not ');
    if (negative) part = part.slice(4).trim();
    part = part.replace(/^only\s+/, '');
    let matched = true;
    const type = part.match(/^(screen|all|print)\b/);
    if (type) {matched = type[1] !== 'print'; part = part.slice(type[0].length).trim().replace(/^and\s*/, '');}
    if (part) for (const clause of part.split(/\s+and\s+/)) {
      const feature = clause.trim().match(/^\(([a-z-]+)\s*:\s*([^()]+)\)$/);
      if (!feature) {matched = false; continue;}
      const name = feature[1], value = feature[2].trim();
      if (['width','min-width','max-width','height','min-height','max-height'].includes(name)) {
        const size = value.match(/^(\d+(?:\.\d+)?)(px|em|rem)$/);
        if (!size) {matched = false; continue;}
        const pixels = Number(size[1]) * (size[2] === 'px' ? 1 : 16), actual = name.endsWith('width') ? innerWidth : innerHeight;
        matched &&= name.startsWith('min-') ? actual >= pixels : name.startsWith('max-') ? actual <= pixels : actual === pixels;
      } else matched &&= ({'prefers-color-scheme':'light','prefers-reduced-motion':'reduce',orientation:'landscape',hover:'none','any-hover':'none',pointer:'none','any-pointer':'none'})[name] === value;
    }
    return negative ? !matched : matched;
  });
}
class MediaQueryList extends EventTarget {
  constructor(media) {super();this.media=String(media);this.onchange=null;}
  get matches() {return __mediaMatches(this.media);}
  addListener(callback) {this.addEventListener('change',callback);}
  removeListener(callback) {this.removeEventListener('change',callback);}
}
globalThis.MediaQueryList = MediaQueryList;
globalThis.matchMedia = query => new MediaQueryList(query);
