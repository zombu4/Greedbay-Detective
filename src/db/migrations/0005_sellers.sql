-- Schema version 5: sellers (owner decisions 2026-10-20: the user's own
-- marked sellers, the low-rating flag, the negative reviews on click).
--
-- lookups: the seller's user id (the "username" of the item page's
-- feedback links; search cards show the same id). NULL for lookups saved
-- before this version.
-- lookup_rows: the feedback score and positive percentage a search card
-- showed for its seller. NULL when the card showed none, and for rows saved
-- before this version.
-- marked_sellers: sellers the user marked, local only (never uploaded or
-- shared); one row per seller id, compared without regard to case.
-- seller_feedback: the seller's negative or neutral reviews as last read
-- from eBay's classic feedback page (data = JSON: the rating counts and the
-- reviews), kept for the time Settings allows.
-- This file is never edited after release; changes go in a new migration.

ALTER TABLE lookups ADD COLUMN seller_id TEXT;

ALTER TABLE lookup_rows ADD COLUMN seller_feedback_count INTEGER;

ALTER TABLE lookup_rows ADD COLUMN seller_positive_percent REAL;

CREATE TABLE marked_sellers (
    seller_id   TEXT PRIMARY KEY COLLATE NOCASE CHECK (length(seller_id) BETWEEN 1 AND 64),
    reason      TEXT NOT NULL CHECK (reason IN ('junk', 'not_as_described', 'scam', 'never_shipped',
                                                'fake_best_offer', 'overpriced_junk', 'other')),
    note        TEXT NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    marked_on   TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE seller_feedback (
    seller_id   TEXT NOT NULL COLLATE NOCASE,
    kind        TEXT NOT NULL CHECK (kind IN ('negative', 'neutral')),
    fetched_at  TEXT NOT NULL,
    data        TEXT NOT NULL,
    PRIMARY KEY (seller_id, kind)
);
