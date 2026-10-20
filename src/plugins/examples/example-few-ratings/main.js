// Example flagger (plugin API 1): a warning when the seller card showed
// fewer than 10 ratings. Nothing when the page showed no count.
var fewest = 10;

function flags(input) {
    var count = input.listing.sellerFeedbackCount;
    if (count === null || count >= fewest)
        return null;
    var words = count === 1 ? "1 rating" : count + " ratings";
    return [{text: "The seller has only " + words, kind: "warn"}];
}
