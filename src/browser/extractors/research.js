// Seller Hub Product Research, SOLD tab (/sh/research), recon section 4.
//
// input: {url, kind: "research"}
// The redirect to /sh/landing (no Product Research access) is recognized
// from the URL before this script runs.
// Key elements: result rows (tr with td.research-table-row__product-info) or
// at least one summary label. With neither, the page is not a result page.
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

// Summary labels, as listed in recon section 4, and the pattern of each value.
const SUMMARY_FIELDS = [
    {label: 'Avg sold price', key: 'avgSoldPriceCents', parse: function (t) { return moneyCents(t, false); }},
    {label: 'Sold price range', key: 'soldPriceRange', parse: priceRange},
    {label: 'Avg shipping', key: 'avgShippingCents', parse: function (t) { return moneyCents(t, false); }},
    {label: 'Free shipping', key: 'freeShippingPercent', parse: percentOf},
    {label: 'Sell-through', key: 'sellThroughPercent', parse: percentOf},
    {label: 'Total sellers', key: 'totalSellers', parse: intOf},
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
function labelElements() {
    const labels = SUMMARY_FIELDS.map(function (field) { return field.label; });
    const found = [];
    for (const element of document.querySelectorAll('body *')) {
        if (element.closest('table'))
            continue;
        const text = norm(element.textContent);
        if (labels.indexOf(text) >= 0 && element.querySelector('*') === null)
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
            if (entry.label !== field.label)
                continue;
            let tile = entry.element.parentElement;
            for (let depth = 0; tile && depth < 4; ++depth, tile = tile.parentElement) {
                const others = found.filter(function (other) {
                    return other.element !== entry.element && tile.contains(other.element);
                });
                if (others.length > 0)
                    break;
                const tileText = norm(tile.textContent);
                const at = tileText.indexOf(field.label);
                const rest = norm(tileText.slice(0, at) + ' ' + tileText.slice(at + field.label.length));
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
    const found = labelElements();
    if (rows.length === 0 && found.length === 0)
        return notExpected('unexpected', 'no research rows and no summary labels');

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
