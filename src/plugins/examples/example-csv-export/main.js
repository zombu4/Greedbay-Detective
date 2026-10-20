// Example exporter (plugin API 1): the lookup's rows as CSV, one line per
// row, totals in dollars. Fields with a comma, a quote or a line break are
// quoted, with quotes doubled (RFC 4180); a field starting with = + - or @
// gets a leading apostrophe, so a spreadsheet never reads it as a formula.
function csvField(value) {
    var text = value === null || value === undefined ? "" : String(value);
    if (/^[=+\-@]/.test(text))
        text = "'" + text;
    if (/[",\r\n]/.test(text))
        return '"' + text.replace(/"/g, '""') + '"';
    return text;
}

function dollars(cents) {
    return cents === null ? "" : (cents / 100).toFixed(2);
}

function exportLookup(input) {
    var lines = ["where,item,total_usd,ebay_condition,date,counted,bucket,title"];
    input.rows.forEach(function (row) {
        lines.push([row.source, row.itemId, dollars(row.totalCents), row.condition, row.date,
                    row.included ? "yes" : "no", row.bucket, row.title].map(csvField).join(","));
    });
    var item = input.listing.itemId.length > 0 ? input.listing.itemId : "lookup";
    return {fileName: "rows-" + item + ".csv", text: lines.join("\r\n") + "\r\n"};
}
