-- Schema version 7: where eBay says an item is, and what its seller's
-- description says (owner decisions 2026-10-21: warn about items located in
-- China and Japan; read the whole listing, its description too).
--
-- lookups.location: the listing's "Located in:" line of the item page, as
-- eBay shows it ("Shenzhen, China").
-- lookups.description: the listing's seller description as text (the item
-- page's description frame), at most 20000 characters; '' when none was
-- read, and description_missing says why.
-- lookups.deep_check_pages: the item pages the Deep check loaded for this
-- lookup (0: it did not run).
-- lookup_rows.location: a search card's text after "Located in " ("China").
-- Empty for lookups and rows saved before this version (nothing is guessed
-- for them).
-- This file is never edited after release; changes go in a new migration.

ALTER TABLE lookups ADD COLUMN location TEXT NOT NULL DEFAULT '';

ALTER TABLE lookups ADD COLUMN description TEXT NOT NULL DEFAULT '' CHECK (length(description) <= 20000);

ALTER TABLE lookups ADD COLUMN description_missing TEXT NOT NULL DEFAULT '';

ALTER TABLE lookups ADD COLUMN deep_check_pages INTEGER NOT NULL DEFAULT 0 CHECK (deep_check_pages >= 0);

ALTER TABLE lookup_rows ADD COLUMN location TEXT NOT NULL DEFAULT '';
