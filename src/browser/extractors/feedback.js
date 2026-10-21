// eBay's classic feedback page of a seller (recon 2026-10-20, "seller
// feedback pages"): /fdbk/feedback_profile/<seller id>?filter=
// feedback_page%3ARECEIVED_AS_SELLER&sort=RELEVANCEV2. The kind to list
// ("negative" or "neutral") comes as input.option: the app requests the URL
// with it as the fragment (never sent to eBay) and passes it on, so the
// page's own scripts cannot change it.
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
// Paging (recon 2026-10-20 "review paging"): nav.pagination with BUTTONs -
// pagination__item (aria-current="page" on the current one) and
// pagination__next (aria-disabled="true" when there is no next page). The
// option "negative@N" (N >= 2) asks for page N of the filtered list: read as
// it is when it shows page N; one click on Next when it shows page N - 1,
// then read once the list shows page N with only that kind. A click on Next
// of a filtered list was not observed in recon: when the list does not
// change, the extractor says so (never guesses).
//
// Reads the counts. When the list already shows exactly the wanted reviews
// (every row of that kind, as many as the 12-month count, at most 25 - also
// when the page is read again after the user acted in the window), it is
// read as it is; otherwise the 12-month count of the wanted kind is clicked
// (when it is not 0) and the list is read once it shows exactly that. The
// buyer column (masked names) is never read.

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

// The seller id from the URL path (percent-decoded; the app's own id check,
// lookup/Sellers.h isSellerId).
function sellerIdOfPage() {
    const match = /^\/fdbk\/feedback_profile\/([^\/]+)\/?$/.exec(location.pathname);
    if (!match)
        return null;
    let id;
    try {
        id = decodeURIComponent(match[1]);
    } catch (e) {
        return null;
    }
    return /^[^\s\/?#&%]{1,64}$/.test(id) ? id : null;
}

// "<seller id> (3,295)" -> 3295; null when not shown.
function scoreOf(sellerId) {
    const pattern = new RegExp('^' + sellerId.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ' \\((\\d{1,3}(?:,\\d{3})+|\\d+)\\)', 'i');
    for (const element of document.querySelectorAll('h1, h2, h3, span, a, div')) {
        if (element.children.length > 3)
            continue;
        const match = pattern.exec(norm(element.textContent));
        if (match)
            return intOf(match[1]);
    }
    return null;
}

// The list's page number and whether it has a next page.
function paging() {
    const nav = document.querySelector('nav.pagination');
    const current = nav ? nav.querySelector('button.pagination__item[aria-current="page"]') : null;
    const next = nav ? nav.querySelector('button.pagination__next') : null;
    return {
        page: current ? intOf(norm(current.textContent)) || 1 : 1,
        next: next && next.getAttribute('aria-disabled') !== 'true' ? next : null,
    };
}

function onlyKind(kind) {
    const rows = listRows();
    return rows.length > 0 && rows.every(function (row) { return ratingOf(row) === kind; });
}

function signature() {
    return listRows().map(function (row) { return norm(row.textContent).slice(0, 80); }).join('|');
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
        // In the FROM cell: "Buyer: x***y (N)", the price, "Verified purchase".
        verified: /Verified purchase/.test(textOf(row.querySelector('.card__from')) || ''),
    };
}

// The list shows exactly the wanted reviews: every row of that kind, as many
// as the page's count (one page holds at most 25).
function listShows(kind, wanted) {
    const rows = listRows();
    return rows.length === Math.min(wanted, 25) && rows.every(function (row) { return ratingOf(row) === kind; });
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', "eBay's error page");
    const sellerId = sellerIdOfPage();
    if (!document.querySelector('table#feedback-cards') || sellerId === null)
        return missingKeyElements('the feedback list (table#feedback-cards) is missing');
    const option = /^(negative|neutral)(?:@(\d{1,4}))?$/.exec(input.option || '');
    if (!option)
        return notExpected('unexpected', 'no review kind (negative or neutral) was asked for');
    const kind = option[1];
    const page = option[2] ? parseInt(option[2], 10) : 1;
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
    if (wanted === 0) {
        base.page = 1;
        base.hasNext = false;
        return expected(base);
    }
    if (page >= 2)
        return nextPage(base, kind, page);
    const button = found.buttons[kind];
    if (!button || wanted < 0)
        return missingKeyElements('the rating count table has no ' + kind + ' count for 12 months');
    if (listShows(kind, wanted)) {
        base.reviews = listRows().map(reviewOf);
        base.page = paging().page;
        base.hasNext = paging().next !== null;
        return expected(base);
    }
    button.click();
    return new Promise(function (resolve) {
        const started = Date.now();
        const timer = setInterval(function () {
            if (listShows(kind, wanted)) {
                clearInterval(timer);
                base.reviews = listRows().slice(0, 25).map(reviewOf);
                base.page = paging().page;
                base.hasNext = paging().next !== null;
                resolve(expected(base));
            } else if (Date.now() - started > WAIT_MS) {
                clearInterval(timer);
                resolve(notExpected('unexpected', 'the list did not switch to the ' + kind + ' reviews'));
            }
        }, POLL_MS);
    });
}

// Page `page` (>= 2) of the filtered list.
function nextPage(base, kind, page) {
    const finish = function () {
        base.reviews = listRows().slice(0, 25).map(reviewOf);
        base.page = page;
        base.hasNext = paging().next !== null;
        return expected(base);
    };
    if (!onlyKind(kind))
        return notExpected('unexpected', 'the list does not show the ' + kind + ' reviews');
    const now = paging();
    if (now.page === page)
        return finish();
    if (now.page !== page - 1 || now.next === null)
        return notExpected('unexpected', 'the list shows page ' + now.page + ', not page ' + (page - 1) + ' with a next page');
    const before = signature();
    now.next.click();
    return new Promise(function (resolve) {
        const started = Date.now();
        const timer = setInterval(function () {
            if (paging().page === page && onlyKind(kind) && signature() !== before) {
                clearInterval(timer);
                resolve(finish());
            } else if (Date.now() - started > WAIT_MS) {
                clearInterval(timer);
                resolve(notExpected('unexpected', 'the list did not move to page ' + page));
            }
        }, POLL_MS);
    });
}
