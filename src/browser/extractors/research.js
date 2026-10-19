// Seller Hub Product Research, SOLD tab (/sh/research), recon section 4 and
// "2026-10-19 real pages".
//
// input: {url, kind: "research"}
// The redirect to /sh/landing (no Product Research access) is recognized
// from the URL before this script runs.
// Key elements: result rows (tr with td.research-table-row__product-info).
// Without rows the page is not (yet) a result page, even when summary labels
// are there (the rows may still be loading) - unless it shows eBay's notice
// "Your search didn't return any results.": then it is the expected page
// with no rows (emptyResult true).
//
// One row is one LISTING, aggregated over all its sales (multi-quantity
// listings sum several sales); avgSoldPrice is the price actually paid,
// accepted Best Offers included (recon section 4).
//
// data:
//   offset, limit, dayRange   from the URL (null when absent)
//   summary                   see readSummary()
//   rows[]                    see readRow()
//   rowCount
//   morePossible              rowCount === limit: a full page, so another page
//                             may exist; null when limit is unknown. The
//                             extractor never fetches further pages itself.

// Summary metrics (recon "2026-10-19 real pages"): div.aggregates holds one
// section.aggregate-metric per metric; inside it, header.metric-title holds
// the label (span.subtitle) and a description sentence, and the rest of the
// section's text is the value.
const SUMMARY_FIELDS = [
    {label: 'Avg sold price', key: 'avgSoldPriceCents', parse: function (t) { return moneyCents(t, false); }},
    {label: 'Sold price range', key: 'soldPriceRange', parse: priceRange},
    {label: 'Avg shipping', key: 'avgShippingCents', parse: function (t) { return moneyCents(t, false); }},
    {label: 'Free shipping', key: 'freeShippingPercent', parse: percentOf},
    {label: 'Sell-through', key: 'sellThroughPercent', parse: percentOf},
    {label: 'Total sellers', key: 'totalSellers', parse: intOf},
];

// "$0.01 – $9,469.00" (EN DASH, as on the page) or "$12.00 - $80.50" ->
// {minCents, maxCents}; else null.
function priceRange(text) {
    const match = /^(\$[\d,]+\.\d{2}) ?[-\u2013] ?(\$[\d,]+\.\d{2})$/.exec(text || '');
    if (!match)
        return null;
    const low = moneyCents(match[1], false);
    const high = moneyCents(match[2], false);
    return low !== null && high !== null && low <= high ? {minCents: low, maxCents: high} : null;
}

// The value text of each metric section, by its label. A section without a
// label or value is skipped; a label shown twice with different values
// gives null for that metric, as does a value that does not match its
// pattern ("Sell-through" can be "-").
function readSummary() {
    const values = {};
    for (const section of document.querySelectorAll('section.aggregate-metric')) {
        const header = section.querySelector('header.metric-title');
        const label = header ? textOf(header.querySelector('.subtitle')) : null;
        if (!label)
            continue;
        // The section's text without its header, wherever the header sits.
        const copy = section.cloneNode(true);
        copy.querySelector('header.metric-title').remove();
        const valueText = norm(copy.textContent);
        if (valueText.length === 0)
            continue;
        (values[label] = values[label] || []).push(valueText);
    }
    const summary = {};
    for (const field of SUMMARY_FIELDS) {
        const texts = values[field.label] || [];
        const distinct = texts.filter(function (t, i) { return texts.indexOf(t) === i; });
        summary[field.key] = distinct.length === 1 ? field.parse(distinct[0]) : null;
    }
    summary.missing = missingFields(summary);
    return summary;
}

function cell(row, name) {
    return row.querySelector('td.research-table-row__' + name);
}

// A table cell as {main, sub}: sub is the text of its div.format (null when
// it has none), main the rest of its text. null for a missing cell.
function cellParts(td) {
    if (!td)
        return null;
    const sub = td.querySelector('.format');
    if (!sub)
        return {main: textOf(td), sub: null};
    const copy = td.cloneNode(true);
    copy.querySelector('.format').remove();
    return {main: textOf(copy), sub: textOf(sub)};
}

function readRow(row) {
    const info = cell(row, 'product-info');
    let itemId = null;
    let title = null;
    if (info) {
        const link = info.querySelector('a[href*="/itm/"]');
        if (link)
            itemId = itemIdFromHref(link.getAttribute('href'));
        for (const anchor of info.querySelectorAll('a')) {
            const text = norm(anchor.textContent);
            if (text.length > 0) {
                title = text;
                break;
            }
        }
        const infoText = textOf(info);
        // Some rows show only the item number instead of a title.
        if (itemId === null && infoText !== null && /^\d+$/.test(infoText))
            itemId = infoText;
        if (title !== null && /^\d+$/.test(title))
            title = null;
    }

    // Value cells (2026-10-19): the amount in one div and div.format below
    // it ("Fixed price", "99% Free shipping"). Recon of 2026-10-18 wrote them
    // as one text, "$38.16 | Fixed price" and "$6.63 | 0% Free shipping";
    // that form is read too.
    const soldParts = cellParts(cell(row, 'avgSoldPrice'));
    const shipParts = cellParts(cell(row, 'avgShippingCost'));
    let avgSoldPriceCents = null;
    let formatText = null;
    if (soldParts && soldParts.sub !== null) {
        avgSoldPriceCents = moneyCents(soldParts.main, false);
        formatText = soldParts.sub.length > 0 ? soldParts.sub : null;
    } else if (soldParts && soldParts.main !== null) {
        const sold = /^(\$[\d,]+\.\d{2})\s*\|?\s*(.*)$/.exec(soldParts.main);
        avgSoldPriceCents = sold ? moneyCents(sold[1], false) : null;
        formatText = sold && sold[2].length > 0 ? sold[2] : null;
    }
    let avgShippingCents = null;
    let freeShippingPercent = null;
    if (shipParts && shipParts.sub !== null) {
        const free = /^(\d+(?:\.\d+)?)% Free shipping$/.exec(shipParts.sub);
        avgShippingCents = moneyCents(shipParts.main, false);
        freeShippingPercent = free ? parseFloat(free[1]) : null;
    } else if (shipParts && shipParts.main !== null) {
        const ship = /^(\$[\d,]+\.\d{2})\s*\|?\s*(\d+(?:\.\d+)?)% Free shipping$/.exec(shipParts.main);
        avgShippingCents = ship ? moneyCents(ship[1], false) : null;
        freeShippingPercent = ship ? parseFloat(ship[2]) : null;
    }
    // A dash when the listing had no bids: "–" (EN DASH) on the page,
    // "-" also accepted.
    const bidsText = textOf(cell(row, 'bids'));

    const record = {
        itemId: itemId,
        title: title,
        avgSoldPriceCents: avgSoldPriceCents,
        formatText: formatText,
        avgShippingCents: avgShippingCents,
        freeShippingPercent: freeShippingPercent,
        totalSold: intOf(textOf(cell(row, 'totalSoldCount'))),
        totalSalesCents: moneyCents(textOf(cell(row, 'totalSalesValue')), false),
        bids: bidsText === '-' || bidsText === '\u2013' ? 0 : intOf(bidsText),
        lastSoldDate: usDate(textOf(cell(row, 'dateLastSold'))),
    };
    record.missing = missingFields(record);
    return record;
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', 'eBay error page');
    const url = new URL(input.url);
    const rows = Array.prototype.filter.call(document.querySelectorAll('tr'), function (row) {
        return cell(row, 'product-info') !== null;
    });
    // eBay's empty result (recon "2026-10-19 phase 4"): a normal outcome with
    // no rows, owner rule 6.
    // Only the sold tab's notice: the active tab has its own (recon).
    const empty = Array.prototype.some.call(document.querySelectorAll('.sold-tab-content .research__generic-error .page-notice__title'),
        function (element) { return /^Your search didn't return any results\./.test(textOf(element)); });
    if (rows.length === 0 && !empty)
        return missingKeyElements('no research result rows');

    const offsetText = url.searchParams.get('offset');
    const limit = intParam(url, 'limit');
    const records = rows.map(readRow);
    return expected({
        offset: offsetText !== null && /^\d+$/.test(offsetText) ? parseInt(offsetText, 10) : null,
        limit: limit,
        dayRange: intParam(url, 'dayRange'),
        summary: readSummary(),
        rows: records,
        rowCount: records.length,
        emptyResult: rows.length === 0,
        morePossible: rows.length === 0 ? false : (limit !== null ? records.length === limit : null),
    });
}
