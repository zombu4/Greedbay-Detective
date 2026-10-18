// Active and sold search results (/sch/i.html), recon sections 1 and 2.
//
// input: {url, kind: "activeSearch" | "soldSearch"}
// Key element: .srp-controls__count-heading ("366 results"). Without it the
// page is not a results page.
//
// data:
//   totalResults        number from the heading, or null
//   totalIsLowerBound   true when the heading shows "<n>+ results"
//   page, perPage       from the URL (_pgn, default 1; _ipg, null when absent)
//   hasMorePages        page * perPage < totalResults; null when either is unknown.
//                       The extractor never fetches further pages itself.
//   skippedDummyCards   cards skipped because they are eBay's "Shop on eBay"
//                       placeholder (item number 123456)
//   items[]             one record per real card, see readCard()
//   unrecognizedRows    attribute rows that matched no known pattern (count only)

// Card attribute rows the app knows to be UI links, not listing data.
const IGNORED_ROWS = ['View similar active items', 'Sell one like this'];

const DUMMY_ITEM_ID = '123456';

function cardItemId(card) {
    const attribute = card.getAttribute('data-listingid');
    if (attribute && /^\d+$/.test(attribute))
        return attribute;
    const link = card.querySelector('a[href*="/itm/"]');
    return link ? itemIdFromHref(link.getAttribute('href')) : null;
}

function isDummyCard(card) {
    if (card.getAttribute('data-listingid') === DUMMY_ITEM_ID)
        return true;
    const link = card.querySelector('a[href*="/itm/"]');
    return link !== null && itemIdFromHref(link.getAttribute('href')) === DUMMY_ITEM_ID;
}

// Title without eBay's "New Listing" prefix and the screen-reader suffix
// "Opens in a new window or tab".
function cleanTitle(text) {
    if (text === null)
        return null;
    const cleaned = text.replace(/^new listing\s*/i, '').replace(/\s*Opens in a new window or tab$/i, '');
    return cleaned.length > 0 ? cleaned : null;
}

// "$300.00" -> {minCents: 30000, maxCents: 30000}; "$10.00 to $20.00" ->
// {minCents: 1000, maxCents: 2000} (listings with variations); else null.
function cardPrice(text) {
    if (text === null)
        return null;
    const single = moneyCents(text, false);
    if (single !== null)
        return {minCents: single, maxCents: single, currency: 'USD'};
    const range = /^(\$[\d,]+\.\d{2}) to (\$[\d,]+\.\d{2})$/.exec(text);
    if (range) {
        const low = moneyCents(range[1], false);
        const high = moneyCents(range[2], false);
        if (low !== null && high !== null && low <= high)
            return {minCents: low, maxCents: high, currency: 'USD'};
    }
    return null;
}

// Reads the attribute rows by pattern, never by position (their order and
// presence vary per card).
function readRows(card, record, counters) {
    const rows = card.querySelectorAll('.s-card__attribute-row');
    for (const row of rows) {
        const text = norm(row.textContent);
        let match;
        if (text.length === 0 || IGNORED_ROWS.indexOf(text) >= 0) {
            continue;
        } else if (moneyCents(text, false) !== null) {
            continue; // the price again; .s-card__price is the source
        } else if ((match = /^(\d{1,3}(?:,\d{3})+|\d+) bids?$/.exec(text))) {
            record.bids = intOf(match[1]);
        } else if (text === 'or Best Offer') {
            record.bestOfferOffered = true;
        } else if (text === 'Buy It Now') {
            record.buyItNow = true;
        } else if (/^best offer accepted$/i.test(text)) {
            record.bestOfferAccepted = true;
        } else if (/^Free delivery(?: |$)/.test(text)) {
            record.shippingCents = 0;
        } else if ((match = /^\+(\$[\d,]+\.\d{2}) delivery(?: |$)/.exec(text))) {
            record.shippingCents = moneyCents(match[1], false);
        } else if ((match = /^Located in (.+)$/.exec(text))) {
            record.location = match[1];
        } else if (text === 'Free returns') {
            record.freeReturns = true;
        } else if (/^[A-Z][a-z]{2}-\d{1,2} \d{1,2}:\d{2}$/.test(text)) {
            record.listedAt = text; // no year on the page; kept as shown
        } else if ((match = /^(.+) (\d{1,3}(?:\.\d+)?)% positive \((\d{1,3}(?:,\d{3})+|\d+)\)$/.exec(text))) {
            record.seller = {name: match[1], positivePercent: parseFloat(match[2]), feedbackCount: intOf(match[3])};
        } else {
            counters.unrecognizedRows += 1;
        }
    }
}

function readCard(card, sold, counters) {
    const priceText = textOf(card.querySelector('.s-card__price'));
    const record = {
        itemId: cardItemId(card),
        title: cleanTitle(textOf(card.querySelector('.s-card__title'))),
        condition: textOf(card.querySelector('.s-card__subtitle')),
        price: cardPrice(priceText),
        priceText: priceText,
        shippingCents: null,
        format: null,
        bids: null,
        buyItNow: false,
        bestOfferOffered: false,
        bestOfferAccepted: false,
        freeReturns: false,
        location: null,
        listedAt: null,
        seller: null,
    };
    readRows(card, record, counters);

    if (record.bids !== null)
        record.format = 'auction';
    else if (record.buyItNow || record.bestOfferOffered || record.bestOfferAccepted)
        record.format = 'fixed_price';

    if (sold) {
        const caption = textOf(card.querySelector('.s-card__caption'));
        const match = caption ? /^Sold (.+)$/.exec(caption) : null;
        record.soldDate = match ? usDate(match[1]) : null;
        // With "Best offer accepted" the shown price is the original asking
        // price; the accepted amount is not on the page (recon section 2).
        record.priceStatus = record.bestOfferAccepted ? 'unconfirmed' : 'shown';
    }
    record.missing = missingFields(record);
    return record;
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', 'eBay error page');
    const heading = document.querySelector('.srp-controls__count-heading');
    if (!heading)
        return notExpected('unexpected', 'no .srp-controls__count-heading');

    const url = new URL(input.url);
    const sold = input.kind === 'soldSearch';
    const headingMatch = /^(\d{1,3}(?:,\d{3})+|\d+)(\+)? results?(?: |$)/.exec(textOf(heading));
    const totalResults = headingMatch ? intOf(headingMatch[1]) : null;
    const page = intParam(url, '_pgn') || 1;
    const perPage = intParam(url, '_ipg');

    const counters = {unrecognizedRows: 0};
    const items = [];
    let skippedDummyCards = 0;
    for (const card of document.querySelectorAll('li.s-card')) {
        if (isDummyCard(card)) {
            skippedDummyCards += 1;
            continue;
        }
        items.push(readCard(card, sold, counters));
    }

    return expected({
        kind: input.kind,
        totalResults: totalResults,
        totalIsLowerBound: headingMatch ? headingMatch[2] === '+' : false,
        page: page,
        perPage: perPage,
        hasMorePages: totalResults !== null && perPage !== null ? page * perPage < totalResults : null,
        skippedDummyCards: skippedDummyCards,
        unrecognizedRows: counters.unrecognizedRows,
        items: items,
    });
}
