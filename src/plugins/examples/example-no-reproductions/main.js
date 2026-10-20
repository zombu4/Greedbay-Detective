// Example matcher (plugin API 1): a reproduction is not the original item.
var words = /\b(reproduction|replica|repro|clone|remake)\b/i;

function match(input) {
    var changes = [];
    input.rows.forEach(function (row) {
        var found = words.exec(row.title);
        if (found && row.bucket !== "not_this_item")
            changes.push({row: row.row, bucket: "not_this_item", reason: "a " + found[1].toLowerCase() + ", not the original"});
    });
    return changes;
}
