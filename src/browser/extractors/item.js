// Item page (/itm/<id>), recon section 3.
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

function stringOrNull(value) {
    return typeof value === 'string' && value.trim().length > 0 ? value.trim() : null;
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

// Element texts that match `pattern` as a whole, e.g. "12 watchers": the
// first captured group of the first matching element, or null.
function wholeTextMatch(pattern) {
    for (const element of document.querySelectorAll('span, div, p, li, a, strong, b')) {
        const match = pattern.exec(norm(element.textContent));
        if (match)
            return match[1];
    }
    return null;
}

// Shipping cost and seller location from the shipping block, without
// returning its text (it contains the viewer's ZIP code).
function shippingFromBlock() {
    const block = document.querySelector('.ux-labels-values--shipping');
    if (!block)
        return {costCents: null, location: null};
    const lines = (block.innerText || block.textContent || '').split('\n').map(norm).filter(function (line) {
        return line.length > 0;
    });
    let costCents = null;
    let location = null;
    for (let i = 0; i < lines.length; ++i) {
        const line = lines[i];
        if (costCents === null) {
            const money = /US \$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})/.exec(line);
            if (money)
                costCents = parseInt(money[1].replace(/,/g, ''), 10) * 100 + parseInt(money[2], 10);
            else if (/^Free\b/.test(line))
                costCents = 0;
        }
        if (location === null) {
            const match = /^Located in:\s*(.*)$/.exec(line);
            if (match)
                location = match[1].length > 0 ? match[1] : (i + 1 < lines.length ? lines[i + 1] : null);
        }
    }
    return {costCents: costCents, location: location};
}

function sellerFromCard() {
    const card = document.querySelector('[data-testid="x-sellercard-atf"]');
    if (!card)
        return null;
    const text = norm(card.textContent);
    const percent = /(\d{1,3}(?:\.\d+)?)% positive/.exec(text);
    const count = /\((\d{1,3}(?:,\d{3})+|\d+)\)/.exec(text);
    let name = null;
    for (const link of card.querySelectorAll('a')) {
        const linkText = norm(link.textContent);
        if (linkText.length > 0) {
            name = linkText;
            break;
        }
    }
    return {
        name: name,
        feedbackCount: count ? intOf(count[1]) : null,
        positivePercent: percent ? parseFloat(percent[1]) : null,
    };
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
        return notExpected('unexpected', 'no JSON-LD Product and no item title heading');

    const offers = product ? asArray(product.offers) : [];
    const offer = offers.length === 1 && offers[0] !== null && typeof offers[0] === 'object' ? offers[0] : null;

    const record = {
        itemId: input.itemId || null,
        title: null,
        titleSource: null,
        priceCents: null,
        currency: null,
        priceSource: null,
        condition: null,
        conditionSource: null,
        availability: offer ? schemaEnum(offer.availability) : null,
        listingStartedAt: offer ? stringOrNull(offer.availabilityStarts) : null,
        shippingOptions: offer ? shippingOptions(offer) : [],
        shippingCents: null,
        location: null,
        bestOfferOffered: document.querySelector('[data-testid="x-offer-action"], .x-offer-action') !== null,
        returnsText: textOf(document.querySelector('.ux-labels-values--returns')),
        seller: sellerFromCard(),
        specifics: itemSpecifics(),
        watchers: intOf(wholeTextMatch(/^(\d{1,3}(?:,\d{3})+|\d+) watchers?$/)),
        soldCount: intOf(wholeTextMatch(/^(\d{1,3}(?:,\d{3})+|\d+) sold$/)),
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
        // "US $269.99", possibly followed by "or Best Offer".
        const match = /^US \$(\d{1,3}(?:,\d{3})+|\d+)\.(\d{2})(?: |$)/.exec(
            textOf(document.querySelector('.x-price-primary')) || '');
        if (match) {
            record.priceCents = parseInt(match[1].replace(/,/g, ''), 10) * 100 + parseInt(match[2], 10);
            record.currency = 'USD';
            record.priceSource = 'dom';
        }
    }

    const jsonCondition = offer ? schemaEnum(offer.itemCondition) : null;
    if (jsonCondition !== null) {
        record.condition = jsonCondition;
        record.conditionSource = 'json-ld';
    } else {
        // "Used" followed by eBay's long explanation: the first word.
        const text = textOf(document.querySelector('.x-item-condition-text'));
        const first = text ? text.split(' ')[0].replace(/:$/, '') : '';
        if (first.length > 0) {
            record.condition = first;
            record.conditionSource = 'dom';
        }
    }

    const shipping = shippingFromBlock();
    record.shippingCents = shipping.costCents;
    record.location = shipping.location;

    record.missing = missingFields(record);
    return expected(record);
}
