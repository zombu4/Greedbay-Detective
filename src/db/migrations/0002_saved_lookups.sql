-- Schema version 2: saved lookups as the app shows them (phase 6).
--
-- Replaces the version-1 tables (lookup, listing, research_summary,
-- valuation, excluded_listing), which held the old value math, with:
--   lookups          one row per lookup: the pasted listing, its identity,
--                    the searches, what was read, and the numbers as last
--                    computed (for the recent-lookups cards)
--   lookup_rows      every candidate row of a lookup: where it came from, its
--                    totals, eBay's condition text, the bucket and reasons the
--                    matcher gave it, and the user's checkbox
--   user_identities  every identity the user entered or corrected for a
--                    listing, with the time (the matcher reads the user's
--                    known-models file in the work folder; this is the record
--                    in the database)
--   listing_photos   the listing's first photo, scaled down, once per item
--                    number, so a saved lookup shows it without loading
--                    anything from eBay
-- Every version-1 value is copied (see the INSERT statements): into its
-- version-2 column where there is one; where there is none, as text the
-- screens show - the lookup's notes (valuation: algo_version, basis,
-- sample_total, shipping_verdict, typical_shipping_cents,
-- subject_shipping_cents) or the row's reasons (excluded_listing.reason,
-- total_sales_cents, the price in a currency other than USD, url when there
-- is no item number). Version-1 rows were never matched, so they get the
-- bucket "to_check" and stay counted unless version 1 had excluded them or
-- their price is not in USD.
--
-- Never stored: page HTML, the user's ZIP code, cookies, the user's own eBay
-- account name (seller names of listings are public listing data).
-- Money is integer cents (USD). Times are UTC ISO-8601 text. Lists are JSON
-- arrays as text. This file is never edited after release; changes go in a
-- new migration.

CREATE TABLE lookups (
    id                      INTEGER PRIMARY KEY,
    created_at              TEXT NOT NULL,
    input                   TEXT NOT NULL,
    item_id                 TEXT,
    marketplace             TEXT NOT NULL,
    status                  TEXT NOT NULL CHECK (status IN ('complete', 'partial', 'failed')),
    message                 TEXT,
    app_version             TEXT NOT NULL,
    note                    TEXT,
    -- The pasted listing as the item page showed it.
    title                   TEXT,
    price_cents             INTEGER,
    shipping_cents          INTEGER,
    currency                TEXT,
    condition_text          TEXT,
    seller_name             TEXT,
    image_url               TEXT,
    specifics               TEXT NOT NULL DEFAULT '[]',
    -- Its identity: where it came from ("item specifics", "known model",
    -- "title", "user", "saved", or "older version" for version-1 rows).
    brand                   TEXT,
    model                   TEXT,
    product_name            TEXT,
    identity_source         TEXT,
    identity_confident      INTEGER NOT NULL DEFAULT 0 CHECK (identity_confident IN (0, 1)),
    identity_reason         TEXT,
    family                  TEXT NOT NULL DEFAULT '[]',
    known_issues            TEXT NOT NULL DEFAULT '[]',
    -- The searches and how deep they went.
    queries                 TEXT NOT NULL DEFAULT '[]',
    sold_depth              INTEGER,
    active_depth            INTEGER,
    research_depth          INTEGER,
    history_days            INTEGER,
    research_unavailable    INTEGER NOT NULL DEFAULT 0 CHECK (research_unavailable IN (0, 1)),
    -- eBay's own Product Research summary block, as the extractor read it.
    research_summary        TEXT NOT NULL DEFAULT '{}',
    notes                   TEXT NOT NULL DEFAULT '[]',
    pages_loaded            INTEGER NOT NULL DEFAULT 0,
    elapsed_ms              INTEGER,
    -- The numbers as last computed from the counted rows.
    listing_total_cents     INTEGER,
    sold_count              INTEGER NOT NULL DEFAULT 0,
    sold_median_cents       INTEGER,
    sold_p25_cents          INTEGER,
    sold_p75_cents          INTEGER,
    sold_confidence         TEXT,
    -- Whether the listing's shipping was flagged, and its ratio to the
    -- reference it was flagged against (NULL when not flagged or when that
    -- reference is $0).
    shipping_flagged        INTEGER NOT NULL DEFAULT 0 CHECK (shipping_flagged IN (0, 1)),
    shipping_flag_ratio     REAL,
    for_sale_count          INTEGER NOT NULL DEFAULT 0,
    cheaper_for_sale        INTEGER NOT NULL DEFAULT 0,
    cheapest_for_sale_cents INTEGER
);

CREATE INDEX lookups_created ON lookups(created_at);

CREATE INDEX lookups_item ON lookups(item_id);

-- Sold and for-sale rows: price, shipping, format ("auction",
-- "fixed_price" or NULL), bids, seller, sold date (sold rows) and whether
-- the price is the price paid. Product Research rows: price_cents = average
-- sold price, shipping_cents = average shipping, format = eBay's format
-- text, total_sold and last_sold_date. position orders the rows of one
-- source as they were collected.
CREATE TABLE lookup_rows (
    id              INTEGER PRIMARY KEY,
    lookup_id       INTEGER NOT NULL REFERENCES lookups(id) ON DELETE CASCADE,
    source          TEXT NOT NULL CHECK (source IN ('sold', 'active', 'research')),
    position        INTEGER NOT NULL,
    item_id         TEXT,
    title           TEXT,
    condition_text  TEXT,
    price_cents     INTEGER,
    price_text      TEXT,
    shipping_cents  INTEGER,
    format          TEXT,
    bids            INTEGER,
    seller_name     TEXT,
    sold_date       TEXT,
    confirmed       INTEGER NOT NULL DEFAULT 0 CHECK (confirmed IN (0, 1)),
    total_sold      INTEGER,
    last_sold_date  TEXT,
    bucket          TEXT NOT NULL CHECK (bucket IN
                        ('same_item', 'documentation', 'part_fits', 'companion', 'accessory', 'lot',
                         'unknown_item', 'to_check', 'not_this_item')),
    reasons         TEXT NOT NULL DEFAULT '[]',
    included        INTEGER NOT NULL CHECK (included IN (0, 1)),
    UNIQUE (lookup_id, source, position)
);

CREATE TABLE user_identities (
    id              INTEGER PRIMARY KEY,
    saved_at        TEXT NOT NULL,
    item_id         TEXT NOT NULL,
    brand           TEXT NOT NULL,
    model           TEXT NOT NULL,
    product_name    TEXT NOT NULL DEFAULT '',
    lookup_id       INTEGER REFERENCES lookups(id) ON DELETE SET NULL
);

CREATE INDEX user_identities_item ON user_identities(item_id);

CREATE TABLE listing_photos (
    item_id     TEXT PRIMARY KEY,
    fetched_at  TEXT NOT NULL,
    image_url   TEXT NOT NULL,
    jpeg        BLOB NOT NULL
);

-- Version-1 lookups. The subject listing (role 'subject', the lowest id when
-- there were several) gives the listing columns; research_summary becomes
-- the summary block in the extractor's key names; valuation gives the
-- numbers (value = median, low/high = P25/P75, sample_kept = count) and,
-- as notes, what version 2 has no column for.
INSERT INTO lookups (
    id, created_at, input, item_id, marketplace, status, message, app_version, note,
    title, price_cents, shipping_cents, currency, condition_text,
    identity_source, queries, notes, research_summary,
    listing_total_cents, sold_count, sold_median_cents, sold_p25_cents, sold_p75_cents, sold_confidence)
SELECT
    l.id, l.created_at, l.input, l.item_id, l.marketplace, l.status, l.error, l.app_version, l.note,
    s.title, s.price_cents, s.shipping_cents, s.currency, s.condition,
    'older version', json_array(l.query),
    CASE WHEN v.lookup_id IS NULL THEN '[]' ELSE json_array(
        printf('version 1 value math (algorithm %d): basis %s, %d of %d sales kept', v.algo_version, v.basis,
               v.sample_kept, v.sample_total),
        printf('version 1 shipping check: %s (listing %s, typical %s)', COALESCE(v.shipping_verdict, 'not recorded'),
               CASE WHEN v.subject_shipping_cents IS NULL THEN 'not recorded'
                    ELSE printf('$%d.%02d', v.subject_shipping_cents / 100, v.subject_shipping_cents % 100) END,
               CASE WHEN v.typical_shipping_cents IS NULL THEN 'not recorded'
                    ELSE printf('$%d.%02d', v.typical_shipping_cents / 100, v.typical_shipping_cents % 100) END)) END,
    COALESCE((SELECT json_object(
                  'avgSoldPriceCents', r.avg_sold_cents,
                  'soldPriceRange', json(CASE WHEN r.min_sold_cents IS NOT NULL AND r.max_sold_cents IS NOT NULL
                                              THEN json_object('minCents', r.min_sold_cents, 'maxCents', r.max_sold_cents)
                                              ELSE 'null' END),
                  'avgShippingCents', r.avg_shipping_cents,
                  'freeShippingPercent', r.free_shipping_pct,
                  'totalSellers', r.sellers_count)
              FROM research_summary r WHERE r.lookup_id = l.id), '{}'),
    CASE WHEN s.price_cents IS NOT NULL AND s.shipping_cents IS NOT NULL THEN s.price_cents + s.shipping_cents END,
    COALESCE(v.sample_kept, 0), v.value_cents, v.low_cents, v.high_cents,
    CASE v.confidence WHEN 'high' THEN 'High' WHEN 'medium' THEN 'Medium' WHEN 'low' THEN 'Low'
                      WHEN 'none' THEN 'Not enough data' END
FROM lookup l
LEFT JOIN listing s ON s.id = (SELECT min(x.id) FROM listing x WHERE x.lookup_id = l.id AND x.role = 'subject')
LEFT JOIN valuation v ON v.lookup_id = l.id;

-- Version-1 comparison rows (every role but 'subject'). The source follows
-- the page it came from; a row whose source was the item page follows its
-- role. position numbers the rows of one lookup and source 0, 1, 2, ... in
-- the order of their version-1 ids (the order the app reads them back in).
-- Formats: 'auction' stays, 'fixed' and 'best_offer' (fixed price with Best
-- Offer) become 'fixed_price', 'unknown' becomes NULL. A price in another
-- currency than USD is not taken as cents of a dollar: price and shipping
-- stay empty, the amount is in the reasons, and the row is not counted.
-- Counted unless excluded_listing listed it (its reason is in the reasons).
INSERT INTO lookup_rows (
    lookup_id, source, position, item_id, title, condition_text, price_cents, shipping_cents, format, bids,
    sold_date, confirmed, total_sold, last_sold_date, bucket, reasons, included)
SELECT
    lookup_id, source, ROW_NUMBER() OVER (PARTITION BY lookup_id, source ORDER BY v1_id) - 1, item_id, title,
    condition_text, price_cents, shipping_cents, format, bids, sold_date, confirmed, total_sold, last_sold_date,
    'to_check', reasons, included
FROM (
    SELECT
        x.id AS v1_id,
        x.lookup_id,
        CASE x.source WHEN 'sold_search' THEN 'sold' WHEN 'active_search' THEN 'active'
                      WHEN 'product_research' THEN 'research' ELSE x.role END AS source,
        x.item_id, x.title, x.condition AS condition_text,
        CASE WHEN COALESCE(x.currency, 'USD') = 'USD' THEN x.price_cents END AS price_cents,
        CASE WHEN COALESCE(x.currency, 'USD') = 'USD' THEN x.shipping_cents END AS shipping_cents,
        CASE x.format WHEN 'auction' THEN 'auction' WHEN 'fixed' THEN 'fixed_price' WHEN 'best_offer' THEN 'fixed_price' END
            AS format,
        x.bids,
        CASE WHEN x.source <> 'product_research' AND x.role = 'sold' THEN substr(x.ended_at, 1, 10) END AS sold_date,
        CASE WHEN x.price_status = 'confirmed' THEN 1 ELSE 0 END AS confirmed,
        CASE WHEN x.source = 'product_research' THEN x.quantity_sold END AS total_sold,
        CASE WHEN x.source = 'product_research' THEN substr(x.ended_at, 1, 10) END AS last_sold_date,
        (SELECT json_group_array(reason) FROM (
             SELECT 1 AS k, 'saved by an older version of the app: not matched' AS reason
             UNION ALL SELECT 2, 'version 1 left it out: ' || e.reason FROM excluded_listing e WHERE e.listing_id = x.id
             UNION ALL SELECT 3, printf('price in %s: %s + %s shipping (hundredths)', x.currency, COALESCE(x.price_cents, 'no'),
                                        COALESCE(x.shipping_cents, 'no'))
                 WHERE COALESCE(x.currency, 'USD') <> 'USD'
             UNION ALL SELECT 4, printf('version 1 price status of a sale: %s', x.price_status)
                 WHERE x.price_status = 'asking' AND x.role = 'sold'
             UNION ALL SELECT 5, printf('version 1 total sales: %d hundredths', x.total_sales_cents)
                 WHERE x.total_sales_cents IS NOT NULL
             UNION ALL SELECT 6, 'address: ' || x.url WHERE x.url IS NOT NULL AND x.item_id IS NULL
             ORDER BY k)) AS reasons,
        CASE WHEN EXISTS (SELECT 1 FROM excluded_listing e WHERE e.listing_id = x.id)
                  OR COALESCE(x.currency, 'USD') <> 'USD' THEN 0 ELSE 1 END AS included
    FROM listing x
    WHERE x.role <> 'subject'
)
ORDER BY lookup_id, source, v1_id;

DROP TABLE excluded_listing;

DROP TABLE valuation;

DROP TABLE research_summary;

DROP TABLE listing;

DROP TABLE lookup;
