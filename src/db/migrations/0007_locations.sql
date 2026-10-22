-- Schema version 7: where eBay says an item is (owner decision 2026-10-21:
-- warn about items located in China and Japan).
--
-- lookups.location: the listing's "Located in:" line of the item page, as
-- eBay shows it ("Shenzhen, China").
-- lookup_rows.location: a search card's "Located in <country>" text after
-- "Located in " ("China"). Empty when the page showed none, and for lookups
-- and rows saved before this version (nothing is guessed for them).
-- This file is never edited after release; changes go in a new migration.

ALTER TABLE lookups ADD COLUMN location TEXT NOT NULL DEFAULT '';

ALTER TABLE lookup_rows ADD COLUMN location TEXT NOT NULL DEFAULT '';
