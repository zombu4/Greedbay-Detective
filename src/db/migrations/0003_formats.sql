-- Schema version 3: listing formats (owner decision 2026-10-19 "Buy It Now
-- vs auction").
--
-- lookups: whether the pasted listing is an auction, its bid count, Buy It
-- Now and Best Offer, the auction end as the item page showed it ("Ends in"
-- text and weekday/time text) and as resolved from them (UTC, and whether
-- it is known to the second); the time it was read is created_at.
-- lookup_rows: Best Offer on a for-sale card, an auction's "time left" and
-- end text as the card showed them, and the resolved end.
-- Existing rows get the defaults: not an auction, no Best Offer, no end.
-- This file is never edited after release; changes go in a new migration.

ALTER TABLE lookups ADD COLUMN auction INTEGER NOT NULL DEFAULT 0 CHECK (auction IN (0, 1));

ALTER TABLE lookups ADD COLUMN bids INTEGER;

ALTER TABLE lookups ADD COLUMN buy_it_now INTEGER NOT NULL DEFAULT 0 CHECK (buy_it_now IN (0, 1));

ALTER TABLE lookups ADD COLUMN best_offer INTEGER NOT NULL DEFAULT 0 CHECK (best_offer IN (0, 1));

ALTER TABLE lookups ADD COLUMN ends_in TEXT;

ALTER TABLE lookups ADD COLUMN ends_at TEXT;

ALTER TABLE lookups ADD COLUMN auction_end TEXT;

ALTER TABLE lookups ADD COLUMN auction_end_seconds INTEGER NOT NULL DEFAULT 0 CHECK (auction_end_seconds IN (0, 1));

ALTER TABLE lookup_rows ADD COLUMN best_offer INTEGER NOT NULL DEFAULT 0 CHECK (best_offer IN (0, 1));

ALTER TABLE lookup_rows ADD COLUMN time_left TEXT;

ALTER TABLE lookup_rows ADD COLUMN auction_end_text TEXT;

ALTER TABLE lookup_rows ADD COLUMN auction_end TEXT;

ALTER TABLE lookup_rows ADD COLUMN auction_end_seconds INTEGER NOT NULL DEFAULT 0 CHECK (auction_end_seconds IN (0, 1));
