-- Schema version 4: the seller's ratings on the pasted listing (phase 8:
-- plugins with the "listing" permission read them, for example the
-- "few seller ratings" flagger).
--
-- lookups: the feedback score and the positive percentage the item page's
-- seller card showed. NULL when it showed none, and for every lookup saved
-- before this version (the values were not kept then).
-- This file is never edited after release; changes go in a new migration.

ALTER TABLE lookups ADD COLUMN seller_feedback_count INTEGER;

ALTER TABLE lookups ADD COLUMN seller_positive_percent REAL;
