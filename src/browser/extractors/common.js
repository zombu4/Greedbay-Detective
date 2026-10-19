// Helpers shared by every extractor.
//
// The extractors run inside the loaded page through the browser's DevTools
// protocol (Runtime.evaluate) in an isolated JavaScript world created for the
// app (Page.createIsolatedWorld): the page's own scripts can neither see nor
// change these functions, while the DOM is the page's. Each extractor returns a plain object; the caller turns it into
// a JSON string, so no HTML ever leaves the page.
//
// Result envelope:
//   {page: "expected", data: {...}}                  the expected page; data per extractor
//   {page: "error_page" | "ship_to" | "unexpected", reason: "..."}  anything else
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

// eBay's "Are you shipping to ...?" prompt (recon "2026-10-19 re-test
// fixes"): div.address-dialog__lightbox, shown (not hidden) on the page.
function shipToPromptShown() {
    const dialog = document.querySelector('.address-dialog__lightbox');
    if (!dialog || dialog.hidden)
        return false;
    const style = getComputedStyle(dialog);
    return style.display !== 'none' && style.visibility !== 'hidden';
}

// The answer for a page without its key elements: eBay's ship-to prompt
// when it is shown (the user answers it; the app never fills in an
// address), otherwise "unexpected" with `reason`.
function missingKeyElements(reason) {
    return shipToPromptShown() ? notExpected('ship_to', 'eBay asks where the items ship to')
                               : notExpected('unexpected', reason);
}

// HTML character references left in text (JSON-LD strings carry them, e.g.
// "Radio &amp; Tube-Tested", owner case 10): &amp; &quot; &#39; &apos;
// &lt; &gt; and numeric ones (&#38; &#x26;), decoded in one pass, so
// "&amp;lt;" becomes "&lt;". Anything else is left as it is.
const NAMED_ENTITIES = {amp: '&', quot: '"', apos: "'", lt: '<', gt: '>'};
function decodeEntities(text) {
    return text.replace(/&(?:#(\d{1,7})|#[xX]([0-9A-Fa-f]{1,6})|(amp|quot|apos|lt|gt));/g, function (all, dec, hex, name) {
        if (name)
            return NAMED_ENTITIES[name];
        const code = dec ? parseInt(dec, 10) : parseInt(hex, 16);
        return code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF) ? String.fromCodePoint(code) : all;
    });
}

// Every string in `value` (objects and arrays included) decoded.
function decodeAll(value) {
    if (typeof value === 'string')
        return value.indexOf('&') >= 0 ? decodeEntities(value) : value;
    if (Array.isArray(value))
        return value.map(decodeAll);
    if (value !== null && typeof value === 'object') {
        const out = {};
        for (const key of Object.keys(value))
            out[key] = decodeAll(value[key]);
        return out;
    }
    return value;
}

// Every text field an extractor returns goes through decodeAll.
function expected(data) {
    return {page: 'expected', data: decodeAll(data)};
}

function notExpected(page, reason) {
    return {page: page, reason: reason};
}
