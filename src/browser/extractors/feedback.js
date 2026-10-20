// eBay's classic feedback page of a seller (recon 2026-10-20, "seller
// feedback pages"): /fdbk/feedback_profile/<seller id>?filter=
// feedback_page%3ARECEIVED_AS_SELLER&sort=RELEVANCEV2, with the kind to list
// in the URL's fragment: #negative or #neutral (the fragment never reaches
// eBay).
//
// The page: "<seller id> (<feedback score>)"; a table of rating counts by
// period whose counts are BUTTONs labeled "negative feedback in last 12
// months" and so on; the list table#feedback-cards (columns FEEDBACK | FROM
// | WHEN, 25 rows) with div.card__rating > svg.icon--feedback-positive /
// -neutral / -negative, div.card__comment, div.card__item (the item title),
// div.card__price, "Verified purchase", and the time in the last cell. A
// click on a count button replaces the list IN PLACE with that kind and
// period (same URL, no page load).
//
// Reads the counts, clicks the 12-month count of the wanted kind (when it is
// not 0), waits until every listed row carries that kind's icon, and returns
// the reviews. The buyer column (masked names) is never read.

const PERIODS = ['1 month', '6 months', '12 months'];
const WAIT_MS = 10000;
const POLL_MS = 250;

function countButtons() {
    const counts = {positive: [-1, -1, -1], neutral: [-1, -1, -1], negative: [-1, -1, -1]};
    const buttons = {};
    for (const button of document.querySelectorAll('button[aria-label]')) {
        const match = /^(positive|neutral|negative) feedback in last (1 month|6 months|12 months)$/i
            .exec(norm(button.getAttribute('aria-label')));
        if (!match)
            continue;
        const kind = match[1].toLowerCase();
        const period = PERIODS.indexOf(match[2].toLowerCase());
        const value = intOf(norm(button.textContent));
        if (period >= 0 && value !== null)
            counts[kind][period] = value;
        if (period === 2)
            buttons[kind] = button;
    }
    return {counts: counts, buttons: buttons};
}

// The seller id from the URL path.
function sellerIdOfPage() {
    const match = /^\/fdbk\/feedback_profile\/([A-Za-z0-9._*-]{1,64})\/?$/.exec(location.pathname);
    return match ? match[1] : null;
}

// "<seller id> (3,295)" -> 3295; null when not shown.
function scoreOf(sellerId) {
    const pattern = new RegExp('^' + sellerId.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ' \\((\\d{1,3}(?:,\\d{3})+|\\d+)\\)');
    for (const element of document.querySelectorAll('h1, h2, h3, span, a, div')) {
        if (element.children.length > 3)
            continue;
        const match = pattern.exec(norm(element.textContent));
        if (match)
            return intOf(match[1]);
    }
    return null;
}

function listRows() {
    const table = document.querySelector('table#feedback-cards');
    if (!table)
        return [];
    return [...table.querySelectorAll('tr')].filter(function (row) { return row.querySelector('.card__rating') !== null; });
}

function ratingOf(row) {
    const icon = row.querySelector('.card__rating svg');
    const classes = icon ? (icon.getAttribute('class') || '') : '';
    const match = /icon--feedback-(positive|neutral|negative)/.exec(classes);
    return match ? match[1] : null;
}

function reviewOf(row) {
    const cells = row.querySelectorAll('td');
    return {
        when: cells.length > 0 ? textOf(cells[cells.length - 1]) : null,
        comment: textOf(row.querySelector('.card__comment')),
        item: textOf(row.querySelector('.card__item')),
        price: textOf(row.querySelector('.card__price')),
        verified: /Verified purchase/.test(row.textContent),
    };
}

function listShows(kind) {
    const rows = listRows();
    return rows.length > 0 && rows.every(function (row) { return ratingOf(row) === kind; });
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', "eBay's error page");
    const sellerId = sellerIdOfPage();
    if (!document.querySelector('table#feedback-cards') || sellerId === null)
        return missingKeyElements('the feedback list (table#feedback-cards) is missing');
    const kind = (location.hash || '').replace(/^#/, '');
    if (kind !== 'negative' && kind !== 'neutral')
        return notExpected('unexpected', 'no review kind (#negative or #neutral) in the address');
    const found = countButtons();
    const base = {
        sellerId: sellerId,
        kind: kind,
        score: scoreOf(sellerId),
        positive: found.counts.positive,
        neutral: found.counts.neutral,
        negative: found.counts.negative,
        reviews: [],
    };
    const wanted = found.counts[kind][2];
    if (wanted === 0)
        return expected(base);
    const button = found.buttons[kind];
    if (!button || wanted < 0)
        return missingKeyElements('the rating count table has no ' + kind + ' count for 12 months');
    button.click();
    return new Promise(function (resolve) {
        const started = Date.now();
        const timer = setInterval(function () {
            if (listShows(kind)) {
                clearInterval(timer);
                base.reviews = listRows().slice(0, 25).map(reviewOf);
                resolve(expected(base));
            } else if (Date.now() - started > WAIT_MS) {
                clearInterval(timer);
                resolve(notExpected('unexpected', 'the list did not switch to the ' + kind + ' reviews'));
            }
        }, POLL_MS);
    });
}
