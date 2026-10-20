// Example valuator (plugin API 1): the median total of the counted sold
// rows whose sold date is within the last 30 days (dates are YYYY-MM-DD).
var days = 30;

function value(input) {
    var since = new Date(Date.now() - days * 24 * 3600 * 1000).toISOString().slice(0, 10);
    var totals = input.rows.filter(function (row) {
        return row.source === "sold" && row.included && row.totalCents !== null && row.date >= since;
    }).map(function (row) { return row.totalCents; }).sort(function (a, b) { return a - b; });
    if (totals.length === 0)
        return null;
    var middle = Math.floor(totals.length / 2);
    var median = totals.length % 2 === 1 ? totals[middle] : Math.round((totals[middle - 1] + totals[middle]) / 2);
    return {label: "Median, last " + days + " days", cents: median,
            note: totals.length === 1 ? "1 sale" : totals.length + " sales"};
}
