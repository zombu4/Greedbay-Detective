-- Schema version 1.
--
-- Money is integer cents plus a currency code. Times are UTC ISO-8601 text.
-- This file is never edited after release; changes go in a new migration.

CREATE TABLE lookup (
    id            INTEGER PRIMARY KEY,
    created_at    TEXT NOT NULL,
    input         TEXT NOT NULL,
    item_id       TEXT,
    query         TEXT NOT NULL,
    marketplace   TEXT NOT NULL,
    status        TEXT NOT NULL CHECK (status IN ('complete', 'partial', 'failed')),
    error         TEXT,
    app_version   TEXT NOT NULL,
    note          TEXT
);

CREATE TABLE listing (
    id                  INTEGER PRIMARY KEY,
    lookup_id           INTEGER NOT NULL REFERENCES lookup(id) ON DELETE CASCADE,
    role                TEXT NOT NULL CHECK (role IN ('subject', 'active', 'sold')),
    source              TEXT NOT NULL CHECK (source IN
                            ('item_page', 'active_search', 'sold_search', 'product_research')),
    item_id             TEXT,
    title               TEXT,
    condition           TEXT,
    format              TEXT CHECK (format IN ('auction', 'fixed', 'best_offer', 'unknown')),
    price_cents         INTEGER,
    currency            TEXT,
    shipping_cents      INTEGER,
    quantity_sold       INTEGER,
    total_sales_cents   INTEGER,
    bids                INTEGER,
    ended_at            TEXT,
    price_status        TEXT NOT NULL CHECK (price_status IN ('confirmed', 'unconfirmed', 'asking')),
    url                 TEXT
);

CREATE INDEX listing_lookup ON listing(lookup_id, role);

CREATE INDEX listing_item ON listing(item_id);

CREATE TABLE research_summary (
    lookup_id           INTEGER PRIMARY KEY REFERENCES lookup(id) ON DELETE CASCADE,
    avg_sold_cents      INTEGER,
    min_sold_cents      INTEGER,
    max_sold_cents      INTEGER,
    free_shipping_pct   REAL,
    avg_shipping_cents  INTEGER,
    sellers_count       INTEGER,
    currency            TEXT
);

CREATE TABLE valuation (
    lookup_id               INTEGER PRIMARY KEY REFERENCES lookup(id) ON DELETE CASCADE,
    algo_version            INTEGER NOT NULL,
    sample_total            INTEGER NOT NULL,
    sample_kept             INTEGER NOT NULL,
    basis                   TEXT NOT NULL CHECK (basis IN ('confirmed', 'unconfirmed', 'none')),
    value_cents             INTEGER,
    low_cents               INTEGER,
    high_cents              INTEGER,
    typical_shipping_cents  INTEGER,
    subject_shipping_cents  INTEGER,
    shipping_verdict        TEXT CHECK (shipping_verdict IN ('above', 'equal', 'below', 'unknown')),
    confidence              TEXT NOT NULL CHECK (confidence IN ('high', 'medium', 'low', 'none')),
    currency                TEXT
);

CREATE TABLE excluded_listing (
    listing_id  INTEGER PRIMARY KEY REFERENCES listing(id) ON DELETE CASCADE,
    reason      TEXT NOT NULL CHECK (reason IN
                    ('mild_outlier', 'extreme_outlier', 'lot', 'condition_mismatch', 'no_price',
                     'other_currency', 'unconfirmed_not_used'))
);
