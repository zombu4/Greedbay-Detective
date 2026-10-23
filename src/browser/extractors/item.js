// Item page (/itm/<id>), recon section 3 and "2026-10-19 real pages".
//
// input: {url, kind: "item", itemId}
// Key elements: a JSON-LD Product (script type="application/ld+json") or the
// title heading (h1 .x-item-title__mainTitle). With neither, the page is not
// an item page.
//
// JSON-LD Product.offers is the first source for price, currency, condition,
// availability, listing start and shipping options; the DOM is the fallback
// for title, price and condition and the only source for the other fields.
// Each *Source field says where a value came from ("json-ld" or "dom").
//
// PRIVACY: the shipping block also shows the viewer's own delivery ZIP code.
// Only the shipping cost and the seller's "Located in" line are taken from
// it; its text is never returned.

const TITLE_SELECTOR = 'h1 .x-item-title__mainTitle, h1.x-item-title__mainTitle';

// Counters eBay shows only on some listings; null when absent, and not
// listed in "missing".
const OPTIONAL_FIELDS = ['watchers', 'soldCount', 'quantityAvailable', 'bids', 'endsIn', 'endsAt'];

// "9 available" in the quantity box (seen 2026-10-19); any other text gives
// null.
function quantityAvailable() {
    const match = /^(\d{1,3}(?:,\d{3})+|\d+) available$/.exec(textOf(document.querySelector('.x-quantity__availability')) || '');
    return match ? intOf(match[1]) : null;
}

// Every JSON-LD node on the page: top-level objects, array members and
// @graph members. Scripts that are not valid JSON are skipped.
function jsonLdNodes() {
    const nodes = [];
    for (const script of document.querySelectorAll('script[type="application/ld+json"]')) {
        let value;
        try {
            value = JSON.parse(script.textContent);
        } catch (e) {
            continue;
        }
        const queue = Array.isArray(value) ? value.slice() : [value];
        while (queue.length > 0) {
            const node = queue.shift();
            if (node === null || typeof node !== 'object')
                continue;
            nodes.push(node);
            if (Array.isArray(node['@graph']))
                queue.push.apply(queue, node['@graph']);
        }
    }
    return nodes;
}

function hasType(node, type) {
    const value = node['@type'];
    return value === type || (Array.isArray(value) && value.indexOf(type) >= 0);
}

// The single JSON-LD Product, or null when there is none or more than one.
function jsonLdProduct() {
    const products = jsonLdNodes().filter(function (node) { return hasType(node, 'Product'); });
    return products.length === 1 ? products[0] : null;
}

// A schema.org price ("269.99", 269.99, "270") -> cents; else null.
function schemaPriceCents(value) {
    const text = typeof value === 'number' ? String(value) : value;
    if (typeof text !== 'string')
        return null;
    const match = /^(\d+)(?:\.(\d{1,2}))?$/.exec(text.trim());
    if (!match)
        return null;
    const fraction = match[2] === undefined ? 0 : parseInt(match[2].padEnd(2, '0'), 10);
    return parseInt(match[1], 10) * 100 + fraction;
}

// "https://schema.org/UsedCondition" -> "UsedCondition"; else null.
function schemaEnum(value) {
    if (typeof value !== 'string')
        return null;
    const match = /^https?:\/\/schema\.org\/([A-Za-z]+)$/.exec(value.trim());
    return match ? match[1] : null;
}

function soldNoticeShown() {
    for (const notice of document.querySelectorAll('.d-statusmessage__notice-live-region')) {
        if (/^This listing sold on /.test(norm(notice.innerText || '')))
            return true;
    }
    return false;
}

function stringOrNull(value) {
    return typeof value === 'string' && value.trim().length > 0 ? value.trim() : null;
}

// The listing's first photo from JSON-LD Product.image (recon section 3:
// the Product has the key "image"): a URL string, the first entry of an
// array, or the "url" of a schema.org ImageObject. Only https addresses;
// anything else gives null.
function imageUrlOf(value) {
    const first = Array.isArray(value) ? value[0] : value;
    let url = null;
    if (typeof first === 'string')
        url = first.trim();
    else if (first !== null && typeof first === 'object' && typeof first.url === 'string')
        url = first.url.trim();
    return url !== null && /^https:\/\/[^\s]+$/.test(url) ? url : null;
}

function asArray(value) {
    if (value === undefined || value === null)
        return [];
    return Array.isArray(value) ? value : [value];
}

function shippingOptions(offer) {
    const options = [];
    for (const detail of asArray(offer.shippingDetails)) {
        if (detail === null || typeof detail !== 'object')
            continue;
        const rate = detail.shippingRate || {};
        const destination = detail.shippingDestination || {};
        options.push({
            amountCents: schemaPriceCents(rate.value),
            currency: stringOrNull(rate.currency),
            country: stringOrNull(destination.addressCountry),
        });
    }
    return options;
}

// The number in the one element whose whole text matches `pattern` (for
// example "12 watchers"). The search starts at text nodes that contain
// `keyword` and climbs at most three parent elements to the first one whose
// whole text matches (the number and the word can be in separate elements).
// When no element or more than one element matches (other listings on the
// page can show such counters), the value is null.
function uniqueWholeTextMatch(pattern, keyword) {
    const matches = new Set();
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
        if (node.data.indexOf(keyword) < 0)
            continue;
        let element = node.parentElement;
        for (let depth = 0; element && depth < 3; ++depth, element = element.parentElement) {
            if (pattern.test(norm(element.textContent))) {
                matches.add(element);
                break;
            }
        }
    }
    if (matches.size !== 1)
        return null;
    return pattern.exec(norm(matches.values().next().value.textContent))[1];
}

// Shipping cost and seller location from the shipping block, without
// returning its text (it contains the viewer's ZIP code). Only the block's
// rendered lines are used (innerText); when it has none, both are null.
//   cost: the one line that starts with "US $<amount>" or "Free"; with no
//         such line or more than one, null
//   location: the text after "Located in:" on the same line, or the next
//         line when the label stands alone; null when it contains any digit
//         (a ZIP code or street number never passes) or is over 100 characters
function shippingFromBlock() {
    const block = document.querySelector('.ux-labels-values--shipping');
    if (!block)
        return {costCents: null, location: null};
    const lines = (block.innerText || '').split('\n').map(norm).filter(function (line) {
        return line.length > 0;
    });
    const costs = [];
    let location = null;
    for (let i = 0; i < lines.length; ++i) {
        const line = lines[i];
        const money = /^US \$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})(?: |$)/.exec(line);
        if (money)
            costs.push(parseInt(money[1].replace(/,/g, ''), 10) * 100 + parseInt(money[2], 10));
        else if (/^Free(?: |$)/.test(line))
            costs.push(0);
        const label = /^Located in:\s*(.*)$/.exec(line);
        if (label && location === null) {
            const value = label[1].length > 0 ? label[1] : (i + 1 < lines.length ? lines[i + 1] : '');
            location = value.length > 0 && value.length <= 100 && !/\d/.test(value) ? value : null;
        }
    }
    return {costCents: costs.length === 1 ? costs[0] : null, location: location};
}

// The seller's user id: the "username" of the page's feedback links for
// this listing (recon 2026-10-20: /fdbk/mweb_profile?...&item_id=<id>&
// username=<id>...; search cards show the same id). Without the item
// number (a page outside www.ebay.com, such as a test page) only when every
// feedback link names the same user. null when none.
// The seller's user id, from the seller card only (architect 2026-10-22:
// never from other links of the page - similar items and ads link to other
// sellers): a feedback link (username=) or a /usr/<id> link inside the card;
// else the page's "feedback received as seller" links of this very item
// (item_id=<this item>, fdbkType=FeedbackReceivedAsSeller), which name this
// listing's seller. Null when neither is there.
function sellerIdOf(card, itemId) {
    const valid = function (id) { return id && /^[^\s\/?#&%]{1,64}$/.test(id) ? id : null; };
    if (card) {
        for (const link of card.querySelectorAll('a[href]')) {
            let url;
            try {
                url = new URL(link.href);
            } catch (e) {
                continue;
            }
            const named = valid(url.pathname.indexOf('/fdbk/') >= 0 ? url.searchParams.get('username') : null);
            if (named)
                return named;
            const user = /^\/usr\/([^\/?#]+)\/?$/.exec(url.pathname);
            if (user && valid(decodeURIComponent(user[1])))
                return decodeURIComponent(user[1]);
        }
    }
    if (!itemId)
        return null;
    for (const link of document.querySelectorAll('a[href*="/fdbk/"]')) {
        let url;
        try {
            url = new URL(link.href);
        } catch (e) {
            continue;
        }
        if (url.searchParams.get('item_id') === itemId && url.searchParams.get('fdbkType') === 'FeedbackReceivedAsSeller') {
            const id = valid(url.searchParams.get('username'));
            if (id)
                return id;
        }
    }
    return null;
}

// The item number in the JSON-LD offer's url (https://www.ebay.com/itm/<digits>), or null.
function offerItemId(offer) {
    const match = offer && typeof offer.url === 'string' ? /^https:\/\/www\.ebay\.com\/itm\/(\d{9,15})(?:[\/?#]|$)/.exec(offer.url) : null;
    return match ? match[1] : null;
}

function sellerFromCard(itemId) {
    const card = document.querySelector('[data-testid="x-sellercard-atf"]');
    if (!card)
        return null;
    const text = norm(card.textContent);
    const percent = /(\d{1,3}(?:\.\d+)?)% positive/.exec(text);
    const count = /\((\d{1,3}(?:,\d{3})+|\d+)\)/.exec(text);
    // The name is the link's bold span (recon: "name link span (BOLD)");
    // the first link of the card can be something else (owner case 11 saved
    // single letters, "F" and "G", as seller names). Without a bold span,
    // the first link with text.
    let name = null;
    const bold = card.querySelector('a span[class*="BOLD"]');
    if (bold && norm(bold.textContent).length > 0)
        name = norm(bold.textContent);
    for (const link of card.querySelectorAll('a')) {
        if (name !== null)
            break;
        const linkText = norm(link.textContent);
        if (linkText.length > 0)
            name = linkText;
    }
    return {
        name: name,
        id: sellerIdOf(card, itemId),
        feedbackCount: count ? intOf(count[1]) : null,
        positivePercent: percent ? parseFloat(percent[1]) : null,
    };
}

// "0 bids", "1 bid", "12 bids" in the bid count; else null.
function auctionBids() {
    const match = /^(\d{1,3}(?:,\d{3})+|\d+) bids?$/.exec(textOf(document.querySelector('[data-testid="x-bid-count"]')) || '');
    return match ? intOf(match[1]) : null;
}

// "3d 22h" from "Ends in 3d 22h" (the label span removed); else null.
function endsInText() {
    const timer = document.querySelector('[data-testid="x-end-time"] .ux-timer__text');
    if (!timer)
        return null;
    const copy = timer.cloneNode(true);
    const label = copy.querySelector('.ux-timer__label');
    if (label)
        label.remove();
    const text = norm(copy.textContent);
    return text.length > 0 ? text : null;
}

function itemSpecifics() {
    const specifics = [];
    for (const list of document.querySelectorAll('dl.ux-layout-section-evo__item--table-view')) {
        for (const label of list.querySelectorAll('dt.ux-labels-values__labels')) {
            const value = label.nextElementSibling;
            if (value && value.tagName === 'DD')
                specifics.push({label: norm(label.textContent).replace(/:$/, ''), value: norm(value.textContent)});
        }
    }
    return specifics;
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', 'eBay error page');
    const product = jsonLdProduct();
    const heading = document.querySelector(TITLE_SELECTOR);
    if (!product && !heading)
        return missingKeyElements('no JSON-LD Product and no item title heading');

    const offers = product ? asArray(product.offers) : [];
    const offer = offers.length === 1 && offers[0] !== null && typeof offers[0] === 'object' ? offers[0] : null;

    const record = {
        itemId: input.itemId || null,
        title: null,
        titleSource: null,
        imageUrl: product ? imageUrlOf(product.image) : null,
        priceCents: null,
        currency: null,
        priceSource: null,
        condition: null,
        conditionSource: null,
        availability: offer ? schemaEnum(offer.availability) : null,
        // A sold listing (recon 2026-10-21): the status notice "This listing
        // sold on <Ddd, Mon dd> at <h:mm> PM." in
        // div.d-statusmessage__notice-live-region, with availability
        // OutOfStock. False when the notice is not shown.
        soldNotice: soldNoticeShown(),
        listingStartedAt: offer ? stringOrNull(offer.availabilityStarts) : null,
        shippingOptions: offer ? shippingOptions(offer) : [],
        shippingCents: null,
        location: null,
        bestOfferOffered: document.querySelector('[data-testid="x-offer-action"], .x-offer-action') !== null,
        // The Buy It Now button (seen on a fixed-price listing 2026-10-19).
        buyItNowOffered: document.querySelector('[data-testid="x-bin-action"]') !== null,
        // Auction (seen 2026-10-19): the bid price block, "Place bid", the
        // bid count ("0 bids") and the end ("Ends in 3d 22h" + "Friday,
        // 06:28 PM", no date). Null when the page does not show them.
        auction: document.querySelector('[data-testid="x-bid-action"], [data-testid="x-bid-price"]') !== null,
        bids: auctionBids(),
        endsIn: endsInText(),
        endsAt: textOf(document.querySelector('[data-testid="x-end-time"] .ux-timer__time-left')),
        returnsText: textOf(document.querySelector('.ux-labels-values--returns')),
        // The listing's own number for its seller links: the address's, else
        // the page's own JSON-LD offer address (a page without an eBay address,
        // as in the tests).
        seller: sellerFromCard(input.itemId || offerItemId(offer)),
        specifics: itemSpecifics(),
        watchers: intOf(uniqueWholeTextMatch(/^(\d{1,3}(?:,\d{3})+|\d+) watchers?$/, 'watcher')),
        soldCount: intOf(uniqueWholeTextMatch(/^(\d{1,3}(?:,\d{3})+|\d+) sold$/, 'sold')),
        quantityAvailable: quantityAvailable(),
        offersInJsonLd: offers.length,
    };

    const jsonTitle = product ? stringOrNull(product.name) : null;
    if (jsonTitle !== null) {
        record.title = jsonTitle;
        record.titleSource = 'json-ld';
    } else if (textOf(heading)) {
        record.title = textOf(heading);
        record.titleSource = 'dom';
    }

    const jsonPrice = offer ? schemaPriceCents(offer.price) : null;
    const jsonCurrency = offer ? stringOrNull(offer.priceCurrency) : null;
    if (jsonPrice !== null && jsonCurrency !== null) {
        record.priceCents = jsonPrice;
        record.currency = jsonCurrency;
        record.priceSource = 'json-ld';
    } else {
        // "US $249.95", possibly followed by "or Best Offer"; any other text
        // after the price (for example a price range) gives no price.
        const match = /^US \$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})(?: or Best Offer)?$/.exec(
            textOf(document.querySelector('.x-price-primary')) || '');
        if (match) {
            record.priceCents = parseInt(match[1].replace(/,/g, ''), 10) * 100 + parseInt(match[2], 10);
            record.currency = 'USD';
            record.priceSource = 'dom';
        }
    }

    // The condition as the page shows it ("Used", "New other (see details)")
    // before eBay's explanation: the first span.ux-textspans in the box (the
    // real page also has a hidden screen-reader copy next to it, recon
    // 2026-10-19), else the first element inside; a trailing colon removed.
    const box = document.querySelector('.x-item-condition-text');
    const visibleElement = box ? (box.querySelector('.ux-textspans') || box.firstElementChild) : null;
    const visible = visibleElement ? norm(visibleElement.textContent).replace(/:$/, '') : '';

    const jsonCondition = offer ? schemaEnum(offer.itemCondition) : null;
    if (jsonCondition !== null) {
        record.condition = jsonCondition;
        record.conditionSource = 'json-ld';
    } else if (visible.length > 0) {
        record.condition = visible;
        record.conditionSource = 'dom';
    }
    // conditionText: what eBay shows. Without it, a schema.org code in
    // eBay's words where that pair was seen on a real page (UsedCondition
    // with "Used", recon 2026-10-19); any other code as it is.
    const SCHEMA_TO_EBAY = {UsedCondition: 'Used'};
    record.conditionText = visible.length > 0 ? visible
                         : (jsonCondition !== null ? (SCHEMA_TO_EBAY[jsonCondition] || jsonCondition) : null);

    const shipping = shippingFromBlock();
    record.shippingCents = shipping.costCents;
    record.location = shipping.location;

    record.missing = missingFields(record).filter(function (key) { return OPTIONAL_FIELDS.indexOf(key) < 0; });
    // Nested values that are missing are listed as "seller.name",
    // "shippingOptions[0].currency" and so on.
    if (record.seller !== null) {
        missingFields(record.seller).forEach(function (key) { record.missing.push('seller.' + key); });
    }
    record.shippingOptions.forEach(function (option, index) {
        missingFields(option).forEach(function (key) { record.missing.push('shippingOptions[' + index + '].' + key); });
    });
    return expected(record);
}
