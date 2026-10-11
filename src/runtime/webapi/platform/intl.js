// Deterministic English/UTC compatibility, not an ICU locale database.
{
  const intl = globalThis.Intl || {};
  const supported =
      locales => [].concat(locales || []).map(String).filter(locale => /^en(?:-|$)/i.test(locale));
  const options = kind => ({locale: 'en-US', ...kind});
  class DateTimeFormat {
    constructor(locales, init = {}) {
      if (init.timeZone && !['UTC', 'Etc/UTC', 'GMT'].includes(init.timeZone))
        throw new RangeError('Only UTC time zones are supported');
      this._options = {...init};
      this.format = this.format.bind(this);
    }
    format(value = Date.now()) {
      return this.formatToParts(value).map(part => part.value).join('');
    }
    formatToParts(value = Date.now()) {
      const date = new Date(Number(value));
      if (!Number.isFinite(date.getTime())) throw new RangeError('Invalid time value');
      const init = this._options, parts = [], pad = value => String(value).padStart(2, '0');
      const append = (type, value) => {
        if (parts.length)
          parts.push({
            type: 'literal',
            value: type === 'hour'                  ? ' ' :
                ['minute', 'second'].includes(type) ? ':' :
                                                      '/'
          });
        parts.push({type, value: String(value)});
      };
      const defaults =
          !['year', 'month', 'day', 'hour', 'minute', 'second', 'weekday'].some(key => init[key]);
      if (init.month || defaults) {
        let month = date.getUTCMonth() + 1;
        if (['long', 'short'].includes(init.month)) {
          month = [
            'January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September',
            'October', 'November', 'December'
          ][month - 1];
          if (init.month === 'short') month = month.slice(0, 3);
        }
        append('month', init.month === '2-digit' ? pad(month) : month);
      }
      if (init.day || defaults)
        append('day', init.day === '2-digit' ? pad(date.getUTCDate()) : date.getUTCDate());
      if (init.year || defaults)
        append(
            'year',
            init.year === '2-digit' ? pad(date.getUTCFullYear() % 100) : date.getUTCFullYear());
      for (const [key, value] of [
               ['hour', date.getUTCHours()], ['minute', date.getUTCMinutes()],
               ['second', date.getUTCSeconds()]])
        if (init[key]) append(key, init[key] === '2-digit' ? pad(value) : value);
      if (init.weekday)
        parts.unshift(
            {
              type: 'weekday',
              value:
                  ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday',
                   'Saturday'][date.getUTCDay()]
                      .slice(0, init.weekday === 'long' ? undefined : 3)
            },
            {type: 'literal', value: ', '});
      return parts;
    }
    resolvedOptions() {
      return options(
          {calendar: 'gregory', numberingSystem: 'latn', ...this._options, timeZone: 'UTC'});
    }
    static supportedLocalesOf(locales) {
      return supported(locales);
    }
  }
  class Collator {
    constructor() {
      this.compare = (left, right) => String(left).localeCompare(String(right));
    }
    resolvedOptions() {
      return options({usage: 'sort', sensitivity: 'variant'});
    }
    static supportedLocalesOf(locales) {
      return supported(locales);
    }
  }
  class PluralRules {
    select(value) {
      return Number(value) === 1 ? 'one' : 'other';
    }
    resolvedOptions() {
      return options({type: 'cardinal'});
    }
    static supportedLocalesOf(locales) {
      return supported(locales);
    }
  }
  class RelativeTimeFormat {
    constructor(locales, init = {}) {
      this._options = {...init};
    }
    format(value, unit) {
      const number = Number(value);
      if (!Number.isFinite(number)) throw new RangeError('Invalid relative time');
      unit = String(unit).replace(/s$/, '');
      if (!['year', 'quarter', 'month', 'week', 'day', 'hour', 'minute', 'second'].includes(unit))
        throw new RangeError('Invalid relative time unit');
      const text = Math.abs(number) + ' ' + unit + (Math.abs(number) === 1 ? '' : 's');
      return number < 0 ? text + ' ago' : 'in ' + text;
    }
    formatToParts(value, unit) {
      return [{type: 'literal', value: this.format(value, unit)}];
    }
    resolvedOptions() {
      return options({style: 'long', numeric: 'always', ...this._options});
    }
    static supportedLocalesOf(locales) {
      return supported(locales);
    }
  }
  class ListFormat {
    constructor(locales, init = {}) {
      this._options = {...init};
    }
    format(values) {
      const list = Array.from(values, String);
      if (list.length < 2) return list[0] || '';
      const last = list.pop();
      return list.join(', ') + (this._options.type === 'disjunction' ? ' or ' : ' and ') + last;
    }
    formatToParts(values) {
      return [{type: 'literal', value: this.format(values)}];
    }
    resolvedOptions() {
      return options({type: 'conjunction', style: 'long', ...this._options});
    }
    static supportedLocalesOf(locales) {
      return supported(locales);
    }
  }
  for (const [name, constructor] of Object.entries(
           {DateTimeFormat, Collator, PluralRules, RelativeTimeFormat, ListFormat}))
    if (!intl[name]) {
      if (['DateTimeFormat', 'Collator'].includes(name)) {
        const callable = function(...args) {
          return Reflect.construct(constructor, args, new.target || constructor);
        };
        Object.defineProperty(callable, 'name', {value: name});
        callable.prototype = constructor.prototype;
        Object.setPrototypeOf(callable, constructor);
        intl[name] = callable;
      } else
        intl[name] = constructor;
    }
  if (!intl.getCanonicalLocales)
    intl.getCanonicalLocales = locales => [...new Set([].concat(locales || []).map(String))];
  globalThis.Intl = intl;
}
