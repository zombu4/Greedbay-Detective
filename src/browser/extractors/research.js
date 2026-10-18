// Seller Hub Product Research, SOLD tab (/sh/research), recon section 4.
//
// input: {url, kind: "research"}
// The redirect to /sh/landing (no Product Research access) is recognized
// from the URL before this script runs.
// Key elements: result rows (tr with td.research-table-row__product-info).
// Without rows the page is not (yet) a result page, even when summary labels
// are there: the rows may still be loading, and a search without sold
// listings is shown to the user instead of being read as "no sales".
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

// Summary labels, as listed in recon section 4, and the pattern of each
// value. Recon writes "Free shipping %" and "Sell-through %"; whether the "%"
// is part of the label was not recorded, so both forms are accepted.
const SUMMARY_FIELDS = [
    {labels: ['Avg sold price'], key: 'avgSoldPriceCents', parse: function (t) { return moneyCents(t, false); }},
    {labels: ['Sold price range'], key: 'soldPriceRange', parse: priceRange},
    {labels: ['Avg shipping'], key: 'avgShippingCents', parse: function (t) { return moneyCents(t, false); }},
    {labels: ['Free shipping', 'Free shipping %'], key: 'freeShippingPercent', parse: percentOf},
    {labels: ['Sell-through', 'Sell-through %'], key: 'sellThroughPercent', parse: percentOf},
    {labels: ['Total sellers'], key: 'totalSellers', parse: intOf},
];

// "$12.00 - $80.50" -> {minCents: 1200, maxCents: 8050}; else null.
function priceRange(text) {
    const match = /^(\$[\d,]+\.\d{2}) ?- ?(\$[\d,]+\.\d{2})$/.exec(text || '');
    if (!match)
        return null;
    const low = moneyCents(match[1], false);
    const high = moneyCents(match[2], false);
    return low !== null && high !== null && low <= high ? {minCents: low, maxCents: high} : null;
}

// Elements outside the results table whose whole text is a summary label.
// Only elements without child elements are looked at, so the text of large
// containers is never built.
function labelElements() {
    const labels = [].concat.apply([], SUMMARY_FIELDS.map(function (field) { return field.labels; }));
    const found = [];
    for (const element of document.querySelectorAll('body *')) {
        if (element.firstElementChild !== null || element.closest('table'))
            continue;
        const text = norm(element.textContent);
        if (labels.indexOf(text) >= 0)
            found.push({element: element, label: text});
    }
    return found;
}

// The summary layout was not recorded in recon, only the labels. A value is
// taken from the smallest element ("tile") around a label that contains no
// other summary label and whose remaining text is exactly one value of the
// expected pattern. So a value can be missing, but never taken from the
// neighbouring metric. A label that appears twice with different values
// gives null. "Sell-through" shown as "-" gives null.
function readSummary(found) {
    const summary = {};
    for (const field of SUMMARY_FIELDS) {
        const values = [];
        for (const entry of found) {
            if (field.labels.indexOf(entry.label) < 0)
                continue;
            let tile = entry.element.parentElement;
            for (let depth = 0; tile && depth < 4; ++depth, tile = tile.parentElement) {
                const others = found.filter(function (other) {
                    return other.element !== entry.element && tile.contains(other.element);
                });
                if (others.length > 0)
                    break;
                const tileText = norm(tile.textContent);
                const at = tileText.indexOf(entry.label);
                const rest = norm(tileText.slice(0, at) + ' ' + tileText.slice(at + entry.label.length));
                if (rest.length === 0)
                    continue;
                const value = field.parse(rest);
                if (value !== null)
                    values.push(JSON.stringify(value));
                break;
            }
        }
        const distinct = values.filter(function (v, i) { return values.indexOf(v) === i; });
        summary[field.key] = distinct.length === 1 ? JSON.parse(distinct[0]) : null;
    }
    summary.missing = missingFields(summary);
    return summary;
}

function cell(row, name) {
    return row.querySelector('td.research-table-row__' + name);
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

    // "$38.16 | Fixed price" (the separator may be missing in the text).
    const soldText = textOf(cell(row, 'avgSoldPrice'));
    const sold = soldText ? /^(\$[\d,]+\.\d{2})\s*\|?\s*(.*)$/.exec(soldText) : null;
    // "$6.63 | 0% Free shipping"
    const shipText = textOf(cell(row, 'avgShippingCost'));
    const ship = shipText ? /^(\$[\d,]+\.\d{2})\s*\|?\s*(\d+(?:\.\d+)?)% Free shipping$/.exec(shipText) : null;
    // "-" when the listing had no bids.
    const bidsText = textOf(cell(row, 'bids'));

    const record = {
        itemId: itemId,
        title: title,
        avgSoldPriceCents: sold ? moneyCents(sold[1], false) : null,
        formatText: sold && sold[2].length > 0 ? sold[2] : null,
        avgShippingCents: ship ? moneyCents(ship[1], false) : null,
        freeShippingPercent: ship ? parseFloat(ship[2]) : null,
        totalSold: intOf(textOf(cell(row, 'totalSoldCount'))),
        totalSalesCents: moneyCents(textOf(cell(row, 'totalSalesValue')), false),
        bids: bidsText === '-' ? 0 : intOf(bidsText),
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
    if (rows.length === 0)
        return notExpected('unexpected', 'no research result rows');
    const found = labelElements();

    const offsetText = url.searchParams.get('offset');
    const limit = intParam(url, 'limit');
    const records = rows.map(readRow);
    return expected({
        offset: offsetText !== null && /^\d+$/.test(offsetText) ? parseInt(offsetText, 10) : null,
        limit: limit,
        dayRange: intParam(url, 'dayRange'),
        summary: readSummary(found),
        rows: records,
        rowCount: records.length,
        morePossible: limit !== null ? records.length === limit : null,
    });
}
