-- Schema version 6: the local model's answers (DESIGN 3.6, item 4c).
--
-- model_answers: one answer of the local model per question, so a re-check
-- or another lookup of the same item asks only about new titles. The key is
-- the SHA-256 (hex) of the model id, the prompt version and the whole
-- prompt; the answer is the model's raw text, which the guards check again
-- every time it is used (a changed guard applies to cached answers too).
-- This file is never edited after release; changes go in a new migration.

CREATE TABLE model_answers (
    question_key  TEXT PRIMARY KEY CHECK (length(question_key) = 64),
    model_id      TEXT NOT NULL,
    prompt_version TEXT NOT NULL,
    answer        TEXT NOT NULL CHECK (length(answer) <= 2000),
    answered_at   TEXT NOT NULL
);
