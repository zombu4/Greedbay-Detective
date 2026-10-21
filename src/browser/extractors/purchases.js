// The user's own eBay purchases page, /mye/myebay/purchase (recon
// 2026-10-20, "the purchases page"): one div.m-ph-card.m-order-card per
// order (25 a page); in it the item link a.nav-link /itm/<item number>, the
// "Order date:" span followed by the date span ("Oct 14, 2026"), and "Sold
// by:" followed by a DIV with a.PSEUDOLINK /usr/<seller id>. Paging:
// nav.pagination with BUTTONs (pagination__item aria-current="page",
// pagination__next aria-disabled="true" on the last page).
//
// For the shared-report purchase check only: per order the item number, the
// seller id and the order month - never the order total, the order number,
// the address or anything else on the page.
//
// input.option "" reads the page as loaded; "@N" (N >= 2) makes the list
// show page N - one click on Next when it shows page N - 1 - and reads it
// (read again after Continue it does not click twice). A click on Next was
// not observed in recon: when the list does not change, the extractor says
// so.

const WAIT_MS = 10000;
const POLL_MS = 250;

function cards() {
    return [...document.querySelectorAll('div.m-ph-card.m-order-card')];
}

// "Oct 14, 2026" -> "2026-10" (MONTHS: common.js); null for anything else.
function monthOf(text) {
    const match = /^([A-Z][a-z]{2}) \d{1,2}, (\d{4})$/.exec(text || '');
    if (!match || !MONTHS[match[1]])
        return null;
    return match[2] + '-' + String(MONTHS[match[1]]).padStart(2, '0');
}

function orderOf(card) {
    const link = card.querySelector('a.nav-link[href*="/itm/"]');
    let itemId = null;
    if (link) {
        const match = /\/itm\/(?:[^/?#]*\/)?(\d{9,15})(?:[/?#]|$)/.exec(link.getAttribute('href') || '');
        itemId = match ? match[1] : null;
    }
    let sellerId = null;
    const soldBy = [...card.querySelectorAll('div, span')].find(function (e) {
        return e.children.length === 0 && norm(e.textContent) === 'Sold by:';
    });
    const sellerBox = soldBy ? soldBy.nextElementSibling : null;
    const sellerLink = sellerBox ? sellerBox.querySelector('a[href*="/usr/"]') : null;
    if (sellerLink) {
        try {
            const path = new URL(sellerLink.getAttribute('href'), location.href).pathname;
            const match = /^\/usr\/([^/]+)\/?$/.exec(path);
            const id = match ? decodeURIComponent(match[1]) : null;
            sellerId = id && /^[^\s/?#&%]{1,64}$/.test(id) ? id : null;
        } catch (e) {
            sellerId = null;
        }
    }
    const label = [...card.querySelectorAll('span')].find(function (e) { return norm(e.textContent) === 'Order date:'; });
    const month = label && label.nextElementSibling ? monthOf(norm(label.nextElementSibling.textContent)) : null;
    return {itemId: itemId, sellerId: sellerId, month: month};
}

function paging() {
    const nav = document.querySelector('nav.pagination');
    const current = nav ? nav.querySelector('button.pagination__item[aria-current="page"]') : null;
    const next = nav ? nav.querySelector('button.pagination__next') : null;
    return {
        page: current ? intOf(norm(current.textContent)) || 1 : 1,
        next: next && next.getAttribute('aria-disabled') !== 'true' ? next : null,
    };
}

function result() {
    const now = paging();
    return expected({orders: cards().map(orderOf), page: now.page, hasNext: now.next !== null});
}

function signature() {
    return cards().map(function (card) { return norm(card.textContent).slice(0, 80); }).join('|');
}

function extract(input) {
    if (isErrorPage())
        return notExpected('error_page', "eBay's error page");
    if (cards().length === 0 && !/^Purchases \| My eBay/.test(document.title))
        return missingKeyElements('no order cards (div.m-ph-card.m-order-card) and not the Purchases page');
    const option = /^(?:@(\d{1,4}))?$/.exec(input.option || '');
    if (!option)
        return notExpected('unexpected', 'the option "' + input.option + '" is not a page');
    const page = option[1] ? parseInt(option[1], 10) : 1;
    const now = paging();
    if (now.page === page)
        return result();
    if (now.page !== page - 1 || now.next === null)
        return notExpected('unexpected', 'the list shows page ' + now.page + ', not page ' + (page - 1) + ' with a next page');
    const before = signature();
    now.next.click();
    return new Promise(function (resolve) {
        const started = Date.now();
        const timer = setInterval(function () {
            if (paging().page === page && signature() !== before) {
                clearInterval(timer);
                resolve(result());
            } else if (Date.now() - started > WAIT_MS) {
                clearInterval(timer);
                resolve(notExpected('unexpected', 'the list did not move to page ' + page));
            }
        }, POLL_MS);
    });
}
