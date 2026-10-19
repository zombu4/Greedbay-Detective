// Active and sold search results (/sch/i.html), recon sections 1 and 2 and
// "2026-10-19 real pages".
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
//                       perPage is the requested _ipg, so this assumes eBay
//                       shows as many results per page as requested (seen
//                       2026-10-19: 240 real cards for _ipg=240).
//                       The extractor never fetches further pages itself.
//   skippedDummyCards   cards skipped because they are eBay's "Shop on eBay"
//                       placeholder: link /itm/123456 (its data-listingid was
//                       123456 on 2026-10-18 and a long number on 2026-10-19)
//   items[]             one record per real card, see readCard()
//   unrecognizedRows    attribute rows that matched no known pattern
//   unrecognizedSamples up to 10 distinct texts of such rows (listing text
//                       only: the rows are inside result cards)

// Card attribute rows the app knows to be UI links, not listing data.
const IGNORED_ROWS = ['View similar active items', 'Sell one like this'];

const DUMMY_ITEM_ID = '123456';

const MAX_UNRECOGNIZED_SAMPLES = 10;

// Fields that many listings simply do not have (an auction-only value, a
// coupon, a counter eBay shows only sometimes). They are null when absent but
// are not listed in "missing", which names only fields every card should
// have. listedAt was seen on 2026-10-18 only; the default sort of 2026-10-19
// showed it on no card.
const OPTIONAL_FIELDS = ['bids', 'timeLeft', 'auctionEnd', 'listedAt', 'originalPriceCents', 'couponPriceCents',
                         'soldCount', 'watchers', 'importFees', 'stockNote', 'multiBuyText'];


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

// The card title without eBay's "New Listing" marker and the screen-reader
// text "Opens in a new window or tab". Both are removed only when they are
// elements of their own inside the title; a title that merely starts with
// such words (for example "New Listings Display Rack") keeps them.
function cardTitle(card) {
    const element = card.querySelector('.s-card__title');
    if (!element)
        return null;
    const copy = element.cloneNode(true);
    for (const child of Array.prototype.slice.call(copy.querySelectorAll('*'))) {
        const text = norm(child.textContent);
        if (/^new listing$/i.test(text) || /^opens in a new window or tab$/i.test(text))
            child.remove();
    }
    const text = norm(copy.textContent);
    return text.length > 0 ? text : null;
}

// "$274.00" -> {minCents: 27400, maxCents: 27400}; "$10.00 to $20.00" ->
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
// presence vary per card). Row shapes: recon "2026-10-19 real pages".
function readRows(card, record, counters) {
    const rows = card.querySelectorAll('.s-card__attribute-row');
    for (const row of rows) {
        const text = norm(row.textContent);
        let match;
        if (text.length === 0 || IGNORED_ROWS.indexOf(text) >= 0) {
            continue;
        } else if (row.querySelector('.s-card__price') !== null || moneyCents(text, false) !== null) {
            // The price row; .s-card__price is the source of the price. A
            // struck-through amount next to it is the earlier price.
            const earlier = row.querySelector('.strikethrough');
            if (earlier !== null)
                record.originalPriceCents = moneyCents(textOf(earlier), false);
        } else if ((match = /^(\d{1,3}(?:,\d{3})+|\d+) bids?(?: · .*)?$/.exec(text))) {
            // "5 bids", or "5 bids · Time left3d 14h left (Thu, 01:11 PM)"
            // with the time in span.s-card__time; both kept as shown (the end
            // has no date).
            record.bids = intOf(match[1]);
            const left = textOf(row.querySelector('.s-card__time-left'));
            const end = textOf(row.querySelector('.s-card__time-end'));
            record.timeLeft = left && left.length > 0 ? left : null;
            const endMatch = end ? /^\((.+)\)$/.exec(end) : null;
            record.auctionEnd = endMatch ? endMatch[1] : null;
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
        } else if (text === 'Includes import fees') {
            record.importFees = 'included';
        } else if (text === 'Import fees due prior to delivery') {
            record.importFees = 'due_before_delivery';
        } else if ((match = /^(\d{1,3}(?:,\d{3})+|\d+)(\+)? sold$/.exec(text))) {
            record.soldCount = {count: intOf(match[1]), atLeast: match[2] === '+'};
        } else if ((match = /^(\d{1,3}(?:,\d{3})+|\d+) watchers?$/.exec(text))) {
            record.watchers = intOf(match[1]);
        } else if (text === 'Last one' || text === 'Almost gone') {
            record.stockNote = text;
        } else if ((match = /^(\$[\d,]+\.\d{2}) with coupon$/.exec(text))) {
            record.couponPriceCents = moneyCents(match[1], false);
        } else if (/^Save up to \d+% when you buy more$/.test(text)) {
            record.multiBuyText = text;
        } else if (/^[A-Z][a-z]{2}-\d{1,2} \d{1,2}:\d{2}$/.test(text)) {
            record.listedAt = text; // no year on the page; kept as shown
        } else if ((match = /^(.+) (\d{1,3}(?:\.\d+)?)% positive \((\d{1,3}(?:,\d{3})+|\d+|\d+(?:\.\d+)?[KM])\)$/.exec(text))) {
            record.seller = {name: match[1], positivePercent: parseFloat(match[2]), feedbackCount: intOf(match[3]),
                             feedbackText: match[3]};
        } else {
            counters.unrecognizedRows += 1;
            if (counters.samples.length < MAX_UNRECOGNIZED_SAMPLES && counters.samples.indexOf(text) < 0)
                counters.samples.push(text);
            // A row about an offer that matches no known text: whether a
            // Best Offer was accepted is then unknown.
            if (/offer/i.test(text))
                record.offerRowUnrecognized = true;
        }
    }
}

function readCard(card, sold, counters) {
    const priceText = textOf(card.querySelector('.s-card__price'));
    const record = {
        itemId: cardItemId(card),
        title: cardTitle(card),
        condition: textOf(card.querySelector('.s-card__subtitle')),
        price: cardPrice(priceText),
        priceText: priceText,
        originalPriceCents: null,
        couponPriceCents: null,
        shippingCents: null,
        importFees: null,
        format: null,
        bids: null,
        timeLeft: null,
        auctionEnd: null,
        buyItNow: false,
        bestOfferOffered: false,
        bestOfferAccepted: false,
        freeReturns: false,
        location: null,
        listedAt: null,
        soldCount: null,
        watchers: null,
        stockNote: null,
        multiBuyText: null,
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
        // A row mentioning an offer in words the app does not know also
        // makes the price unconfirmed.
        record.priceStatus = record.bestOfferAccepted || record.offerRowUnrecognized ? 'unconfirmed' : 'shown';
    }
    delete record.offerRowUnrecognized;
    record.missing = missingFields(record).filter(function (key) { return OPTIONAL_FIELDS.indexOf(key) < 0; });
    // An abbreviated feedback count ("5.9K") is shown, just not exactly.
    if (record.seller !== null && record.seller.feedbackCount === null)
        record.missing.push('seller.feedbackCount');
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
    // _pgn absent = page 1; present but not a positive integer = unknown.
    const page = url.searchParams.get('_pgn') === null ? 1 : intParam(url, '_pgn');
    const perPage = intParam(url, '_ipg');

    const counters = {unrecognizedRows: 0, samples: []};
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
        totalIsLowerBound: headingMatch ? headingMatch[2] === '+' : null,
        page: page,
        perPage: perPage,
        hasMorePages: totalResults !== null && perPage !== null && page !== null ? page * perPage < totalResults : null,
        skippedDummyCards: skippedDummyCards,
        unrecognizedRows: counters.unrecognizedRows,
        unrecognizedSamples: counters.samples,
        items: items,
    });
}
