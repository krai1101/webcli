// Unicode boundaries come from ICU. JavaScript owns API coercion and brands;
// native cursors own UTF-16 text and are finalized with their document runtime.
{
  const segmenters = new WeakMap(), segments = new WeakMap(), iterators = new WeakMap();
  const native = __webcliSegmenter;
  const toString = value => {
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a string');
    return String(value);
  };
  const requireState = (map, value) => {
    const state = map.get(value);
    if (!state) throw new TypeError('Incompatible segmentation receiver');
    return state;
  };
  function canonicalTag(value) {
    const tag = toString(value), parts = tag.split('-');
    if (!/^(?:[a-z]{2,3}|[a-z]{5,8})$/i.test(parts[0]))
      throw new RangeError('Invalid language tag');
    let index = 1;
    if (/^[a-z]{4}$/i.test(parts[index] || '')) ++index;
    if (/^(?:[a-z]{2}|\d{3})$/i.test(parts[index] || '')) ++index;
    const variants = new Set(), extensions = new Set();
    while (/^(?:[a-z0-9]{5,8}|\d[a-z0-9]{3})$/i.test(parts[index] || '')) {
      const variant = parts[index++].toLowerCase();
      if (variants.has(variant)) throw new RangeError('Duplicate locale variant');
      variants.add(variant);
    }
    while (index < parts.length) {
      const key = parts[index++].toLowerCase();
      if (!/^[a-z0-9]$/.test(key) || extensions.has(key))
        throw new RangeError('Invalid locale extension');
      extensions.add(key);
      const start = index, pattern = key === 'x' ? /^[a-z0-9]{1,8}$/i : /^[a-z0-9]{2,8}$/i;
      while (index < parts.length && pattern.test(parts[index])) ++index;
      if (index === start || key === 'x' && index !== parts.length)
        throw new RangeError('Invalid locale extension');
    }
    return native('canonical', tag);
  }
  function localeList(locales) {
    if (locales === undefined) return [];
    if (locales === null) throw new TypeError('Locales cannot be null');
    const list = typeof locales === 'string' ? [locales] : Object(locales);
    const result = [], length = Math.min(Math.max(Math.trunc(+list.length) || 0, 0),
                                       Number.MAX_SAFE_INTEGER);
    for (let index = 0; index < length; ++index) {
      if (!(index in list)) continue;
      const value = list[index];
      if (typeof value !== 'string' && (value === null ||
          typeof value !== 'object' && typeof value !== 'function'))
        throw new TypeError('Locale must be a string or object');
      const tag = canonicalTag(value);
      if (!result.includes(tag)) result.push(tag);
    }
    return result;
  }
  function optionsObject(options) {
    if (options === undefined) return Object.create(null);
    if (options === null) throw new TypeError('Options cannot be null');
    return Object(options);
  }
  function option(options, name, allowed, fallback) {
    const value = options[name];
    if (value === undefined) return fallback;
    const text = toString(value);
    if (!allowed.includes(text)) throw new RangeError('Invalid ' + name);
    return text;
  }
  function record(state, bounds) {
    if (bounds === null || bounds === undefined) return undefined;
    const value = {segment: state.input.slice(bounds[0], bounds[1]),
                   index: bounds[0], input: state.input};
    if (state.granularity === 'word') value.isWordLike = bounds[2];
    return value;
  }
  class SegmentIterator {
    next() {
      const state = requireState(iterators, this);
      const bounds = state.cursor ? native('next', state.cursor) : null;
      if (bounds === null) {
        state.cursor = null;
        return {value: undefined, done: true};
      }
      return {value: record(state, bounds), done: false};
    }
    [Symbol.iterator]() { return this; }
  }
  class Segments {
    containing(index) {
      const state = requireState(segments, this);
      let position = +index;
      position = Number.isNaN(position) ? 0 : Math.trunc(position);
      if (position < 0 || position >= state.input.length) return undefined;
      return record(state, native('containing', state.cursor, position));
    }
    [Symbol.iterator]() {
      const state = requireState(segments, this), iterator = new SegmentIterator();
      iterators.set(iterator, {...state, cursor: native('clone', state.cursor)});
      return iterator;
    }
  }
  class Segmenter {
    constructor(locales = undefined, options = undefined) {
      const requested = localeList(locales), opts = optionsObject(options);
      option(opts, 'localeMatcher', ['lookup', 'best fit'], 'best fit');
      const granularity = option(opts, 'granularity', ['grapheme', 'word', 'sentence'], 'grapheme');
      const locale = requested.map(tag => native('lookup', tag)).find(Boolean) || 'en-US';
      segmenters.set(this, {locale, granularity});
    }
    segment(input) {
      const state = requireState(segmenters, this), text = toString(input);
      const result = Object.create(Segments.prototype);
      segments.set(result, {input: text, granularity: state.granularity,
          cursor: native('create', state.locale, state.granularity, text)});
      return result;
    }
    resolvedOptions() {
      const state = requireState(segmenters, this);
      return {locale: state.locale, granularity: state.granularity};
    }
    static supportedLocalesOf(locales, options = undefined) {
      const requested = localeList(locales), opts = optionsObject(options);
      option(opts, 'localeMatcher', ['lookup', 'best fit'], 'best fit');
      return requested.filter(tag => native('lookup', tag) !== null);
    }
  }
  Object.setPrototypeOf(SegmentIterator.prototype,
      Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
  delete Segments.prototype.constructor;
  delete SegmentIterator.prototype.constructor;
  for (const [prototype, tag] of [[Segmenter.prototype, 'Intl.Segmenter'],
      [SegmentIterator.prototype, 'Segmenter String Iterator']])
    Object.defineProperty(prototype, Symbol.toStringTag, {value: tag, configurable: true});
  Object.defineProperty(Intl, 'Segmenter', {value: Segmenter, writable: true, configurable: true});
}
