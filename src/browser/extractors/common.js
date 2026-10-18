// Helpers shared by every extractor.
//
// The extractors run inside the loaded page through
// QWebEnginePage::runJavaScript in an isolated JavaScript world: the page's
// own scripts can neither see nor change these functions, while the DOM is
// the page's. Each extractor returns a plain object; the caller turns it into
// a JSON string, so no HTML ever leaves the page.
//
// Result envelope:
//   {page: "expected", data: {...}}                  the expected page; data per extractor
//   {page: "error_page" | "unexpected", reason: "..."}  anything else
// Inside data, a field the page did not provide is null and its name is
// listed in the record's "missing" array. Nothing is guessed: a text that
// does not match its exact pattern gives null, never a nearby value.
// Money is in integer cents (USD; ebay.com only), dates are YYYY-MM-DD.

// Collapses runs of white space to one space and trims.
function norm(text) {
    return (text || '').replace(/\s+/g, ' ').trim();
}

// Normalized text of an element, or null when the element is absent.
function textOf(element) {
    return element ? norm(element.textContent) : null;
}

// "$1,234.56" -> 123456. With allowUsPrefix, "US $269.99" too. Anything
// else -> null.
function moneyCents(text, allowUsPrefix) {
    if (text === null || text === undefined)
        return null;
    const pattern = allowUsPrefix ? /^(?:US )?\$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})$/
                                  : /^\$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})$/;
    const match = pattern.exec(text);
    if (!match)
        return null;
    return parseInt(match[1].replace(/,/g, ''), 10) * 100 + parseInt(match[2], 10);
}

// "1,234" or "1234" -> 1234; anything else -> null.
function intOf(text) {
    if (text === null || text === undefined)
        return null;
    if (!/^(\d{1,3}(?:,\d{3})+|\d+)$/.test(text))
        return null;
    return parseInt(text.replace(/,/g, ''), 10);
}

// "45%" or "12.5%" -> 45 / 12.5; anything else -> null.
function percentOf(text) {
    if (text === null || text === undefined)
        return null;
    const match = /^(\d+(?:\.\d+)?)%$/.exec(text);
    return match ? parseFloat(match[1]) : null;
}

const MONTHS = {Jan: 1, Feb: 2, Mar: 3, Apr: 4, May: 5, Jun: 6, Jul: 7, Aug: 8, Sep: 9, Oct: 10, Nov: 11, Dec: 12};

// "Oct 18, 2026" -> "2026-10-18"; anything else (including an impossible
// day) -> null.
function usDate(text) {
    if (text === null || text === undefined)
        return null;
    const match = /^([A-Z][a-z]{2}) (\d{1,2}), (\d{4})$/.exec(text);
    if (!match || !(match[1] in MONTHS))
        return null;
    const year = parseInt(match[3], 10);
    const month = MONTHS[match[1]];
    const day = parseInt(match[2], 10);
    const check = new Date(Date.UTC(year, month - 1, day));
    if (check.getUTCFullYear() !== year || check.getUTCMonth() !== month - 1 || check.getUTCDate() !== day)
        return null;
    return year + '-' + String(month).padStart(2, '0') + '-' + String(day).padStart(2, '0');
}

// The item number in an /itm/<digits> link, or null.
function itemIdFromHref(href) {
    const match = /\/itm\/(\d+)(?:[/?#]|$)/.exec(href || '');
    return match ? match[1] : null;
}

// A URL query parameter as a positive integer, or null when absent or not a
// positive integer.
function intParam(url, name) {
    const value = url.searchParams.get(name);
    if (value === null || !/^\d+$/.test(value))
        return null;
    const number = parseInt(value, 10);
    return number > 0 ? number : null;
}

// Names of the fields of `record` whose value is null.
function missingFields(record) {
    return Object.keys(record).filter(function (key) { return record[key] === null; });
}

// eBay's error page, as observed on 2026-10-18 (HTTP 403, signed out):
// <title>Error Page | eBay</title> and div.error-header with #error-info.
function isErrorPage() {
    return document.title === 'Error Page | eBay' || document.querySelector('.error-header #error-info') !== null;
}

function expected(data) {
    return {page: 'expected', data: data};
}

function notExpected(page, reason) {
    return {page: page, reason: reason};
}
