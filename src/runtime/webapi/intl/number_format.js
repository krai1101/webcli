// ICU already backs segmentation. Reuse it for locale-aware decimal, percent
// and currency formatting instead of inventing symbols or grouping rules.
{
  const native = __webcliNumberFormat, states = new WeakMap();
  const supported = Intl.Segmenter.supportedLocalesOf.bind(Intl.Segmenter);
  const canonical = __webcliSegmenter;
  const option = (options, name, values, fallback) => {
    const value = options[name];
    if (value === undefined) return fallback;
    const text = String(value);
    if (!values.includes(text)) throw new RangeError('Invalid ' + name);
    return text;
  };
  const digits = (value, minimum, maximum, fallback) => {
    if (value === undefined) return fallback;
    const number = Number(value);
    if (!Number.isFinite(number) || number < minimum || number > maximum)
      throw new RangeError('Invalid digit count');
    return Math.floor(number);
  };
  const get = object => {
    const state = states.get(object);
    if (!state) throw new TypeError('Intl.NumberFormat receiver required');
    return state;
  };
  const parts = (state, input) => {
    const value = typeof input === 'bigint' ? String(input) : +input;
    return native('parts', state.handle, value).map(part => ({type: part[0], value: part[1]}));
  };
  function NumberFormat(locales, options = undefined) {
    if (!new.target) return new NumberFormat(locales, options);
    if (options === null) throw new TypeError('Options cannot be null');
    const opts = options === undefined ? {} : Object(options);
    const localeMatcher = option(opts, 'localeMatcher', ['lookup', 'best fit'], 'best fit');
    const requested = supported(locales, {localeMatcher});
    const locale = canonical('canonical', requested[0] || 'en-US');
    const style = option(opts, 'style', ['decimal', 'percent', 'currency'], 'decimal');
    let currency;
    if (opts.currency !== undefined) {
      currency = String(opts.currency).toUpperCase();
      if (!/^[A-Z]{3}$/.test(currency)) throw new RangeError('Invalid currency code');
    }
    if (style === 'currency' && currency === undefined) throw new TypeError('Currency required');
    const currencyDisplay =
        option(opts, 'currencyDisplay', ['symbol', 'narrowSymbol', 'code', 'name'], 'symbol');
    const currencySign = option(opts, 'currencySign', ['standard', 'accounting'], 'standard');
    const notation = option(opts, 'notation', ['standard', 'compact', 'scientific', 'engineering'], 'standard');
    const compactDisplay = option(opts, 'compactDisplay', ['short', 'long'], 'short');
    const signDisplay =
        option(opts, 'signDisplay', ['auto', 'never', 'always', 'exceptZero', 'negative'], 'auto');
    const minimumIntegerDigits = digits(opts.minimumIntegerDigits, 1, 21, 1);
    const baseFraction = style === 'currency' ? native('currencyDigits', currency) : 0;
    const requestedMin = digits(opts.minimumFractionDigits, 0, 100, undefined);
    const requestedMax = digits(opts.maximumFractionDigits, 0, 100, undefined);
    const minimumFractionDigits = requestedMin ?? Math.min(baseFraction, requestedMax ?? 100);
    const maximumFractionDigits =
        requestedMax ?? Math.max(minimumFractionDigits, style === 'decimal' ? 3 : baseFraction);
    if (minimumFractionDigits > maximumFractionDigits)
      throw new RangeError('Invalid fraction digits');
    const minimumSignificantDigits = digits(opts.minimumSignificantDigits, 1, 21, undefined);
    const maximumSignificantDigits = digits(opts.maximumSignificantDigits, 1, 21, undefined);
    const compactDefaults = notation === 'compact' && requestedMin === undefined &&
        requestedMax === undefined && minimumSignificantDigits === undefined &&
        maximumSignificantDigits === undefined;
    let grouping = opts.useGrouping;
    if (grouping === undefined || grouping === 'true' || grouping === 'false')
      grouping = notation === 'compact' ? 'min2' : 'auto';
    else if (typeof grouping !== 'string')
      grouping = grouping ? 'always' : false;
    else if (!['auto', 'always', 'min2'].includes(grouping))
      throw new RangeError('Invalid useGrouping');
    const roundingMode = option(opts, 'roundingMode', ['halfExpand'], 'halfExpand');
    option(opts, 'roundingPriority', ['auto'], 'auto');
    option(opts, 'trailingZeroDisplay', ['auto'], 'auto');
    if (opts.roundingIncrement !== undefined && Number(opts.roundingIncrement) !== 1)
      throw new RangeError('Rounding increments are unsupported');
    const skeleton = [
      'rounding-mode-half-up',
      'integer-width/+' +
          '0'.repeat(minimumIntegerDigits),
      grouping === false        ? 'group-off' :
          grouping === 'always' ? 'group-on-aligned' :
          grouping === 'min2'   ? 'group-min2' :
                                  'group-auto'
    ];
    const resolved = {
      locale,
      numberingSystem: 'latn',
      style,
      minimumIntegerDigits,
      useGrouping: grouping,
      notation,
      signDisplay,
      roundingMode,
      roundingIncrement: 1,
      roundingPriority: 'auto',
      trailingZeroDisplay: 'auto'
    };
    if (notation !== 'standard')
      skeleton.push(notation === 'compact' ? 'compact-' + compactDisplay : notation);
    if (notation === 'compact') resolved.compactDisplay = compactDisplay;
    if (compactDefaults) {
      // ICU's relaxed precision selects the more precise of integer and
      // two significant digits, matching ECMA-402 compact defaults.
      skeleton.push('./@#r');
      Object.assign(resolved, {minimumFractionDigits: 0, maximumFractionDigits: 0,
        minimumSignificantDigits: 1, maximumSignificantDigits: 2,
        roundingPriority: 'morePrecision'});
    } else if (minimumSignificantDigits !== undefined || maximumSignificantDigits !== undefined) {
      const low = minimumSignificantDigits ?? 1, high = maximumSignificantDigits ?? 21;
      if (low > high) throw new RangeError('Invalid significant digits');
      skeleton.push('@'.repeat(low) + '#'.repeat(high - low));
      Object.assign(resolved, {minimumSignificantDigits: low, maximumSignificantDigits: high});
    } else {
      skeleton.push(
          maximumFractionDigits === 0 ? 'precision-integer' :
                                        '.' +
                  '0'.repeat(minimumFractionDigits) +
                  '#'.repeat(maximumFractionDigits - minimumFractionDigits));
      Object.assign(resolved, {minimumFractionDigits, maximumFractionDigits});
    }
    if (style === 'currency') {
      skeleton.push('currency/' + currency, {
        symbol: 'unit-width-short',
        narrowSymbol: 'unit-width-narrow',
        code: 'unit-width-iso-code',
        name: 'unit-width-full-name'
      }[currencyDisplay]);
      Object.assign(resolved, {currency, currencyDisplay, currencySign});
    } else if (style === 'percent')
      skeleton.push('percent', 'scale/100');
    const sign = {
      auto: 'auto',
      always: 'always',
      never: 'never',
      exceptZero: 'except-zero',
      negative: 'negative'
    }[signDisplay];
    skeleton.push(
        style === 'currency' && currencySign === 'accounting' && signDisplay !== 'never' ?
            'sign-accounting' + (signDisplay === 'auto' ? '' : '-' + sign) :
            'sign-' + sign);
    if (opts.numberingSystem !== undefined) {
      const system = String(opts.numberingSystem);
      if (!/^[a-z0-9]{3,8}$/i.test(system)) throw new RangeError('Invalid numbering system');
      skeleton.push('numbering-system/' + system.toLowerCase());
      resolved.numberingSystem = system.toLowerCase();
    }
    resolved.numberingSystem = native(
        'numberingSystem', locale,
        opts.numberingSystem === undefined ? '' : resolved.numberingSystem);
    states.set(this, {handle: native('create', locale, skeleton.join(' ')), resolved, bound: null});
  }
  Object.defineProperties(NumberFormat.prototype, {
    format: {
      configurable: true,
      get() {
        const state = get(this);
        if (!state.bound)
          state.bound = value => parts(state, value).map(part => part.value).join('');
        return state.bound;
      }
    },
    formatToParts: {
      configurable: true,
      writable: true,
      value(value) {
        return parts(get(this), value);
      }
    },
    resolvedOptions: {
      configurable: true,
      writable: true,
      value() {
        return {...get(this).resolved};
      }
    },
    [Symbol.toStringTag]: {configurable: true, value: 'Intl.NumberFormat'}
  });
  NumberFormat.supportedLocalesOf = (locales, options) => supported(locales, options);
  Object.defineProperty(
      Intl, 'NumberFormat', {value: NumberFormat, configurable: true, writable: true});
  const numberValue = Number.prototype.valueOf, bigintValue = BigInt.prototype.valueOf;
  Object.defineProperty(Number.prototype, 'toLocaleString', {
    configurable: true,
    writable: true,
    value(locales, options) {
      return new NumberFormat(locales, options).format(numberValue.call(this));
    }
  });
  Object.defineProperty(BigInt.prototype, 'toLocaleString', {
    configurable: true,
    writable: true,
    value(locales, options) {
      return new NumberFormat(locales, options).format(bigintValue.call(this));
    }
  });
}
