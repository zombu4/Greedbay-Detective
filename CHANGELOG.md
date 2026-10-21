# Changelog

Every release has a section here; the release workflow publishes the
section of its version as the release notes. Versions follow
MAJOR.MINOR.PATCH.

## 0.1.0

Not released yet.

### What it does

- Paste an eBay listing link (or drop it on the window): the app reads the
  listing in your own installed browser (Brave, Google Chrome, Microsoft
  Edge; on Linux also Chromium), in a profile of its own, and finds out what
  the item is.
- What the same item really sold for: the sold search and eBay's Product
  Research (real accepted Best Offer prices where eBay has them), totals
  including shipping, median and middle half, how sure the numbers are.
- What is for sale now, cheapest first; Buy It Now and auctions told apart,
  with the time left.
- Flags: shipping far above normal, cheaper listings of the same item for
  sale right now, "likely broken" when eBay or the title says so.
- Sellers: mark a seller you had trouble with (got junk, not as described,
  scam, never shipped, fake Best Offer, overpriced junk, other) with your own
  note - kept only on your computer; a marked seller is flagged on every
  later lookup. Sellers below 96% positive are flagged (the limit is a
  setting; sellers with very few ratings are not judged). A click on a
  seller shows the seller's negative or neutral reviews of the last 12
  months from eBay's feedback page.
- Rows that are not the same item are kept apart, each with its reason:
  manuals, parts, kits, accessories, lots, other models, software and
  controllers for game systems, re-issues, merchandise. You can tick or
  untick any row; the numbers follow at once.
- If the app is not sure which model a listing is, it asks you; your answer
  is saved and used again.
- Every lookup is saved. Price history shows each sale of an item over 90
  days, 1 year or 3 years, the listings you checked, and "Re-check now".
- Plugins: small JavaScript add-ons in a sandbox (no files, no network),
  off until you turn them on. Four examples come with the app.
- Export all lookups as CSV and JSON, or one lookup as JSON, into your
  Downloads folder.
- Installers for macOS (Apple Silicon), Windows and Ubuntu 24.04, each with a
  full uninstall that asks whether to keep your saved lookups.
- A daily cap on eBay pages and a pause between pages, both in Settings.
- A seller's negative or neutral reviews of the last 12 months are read page
  by page beyond the first 25 (paced, inside the daily cap, kept for the
  days set in Settings); a read that stops early says how far it got.
- Shared seller list (off until you turn it on): reports by other users,
  signed by the list owner's keys and fetched only through Tor; report a
  seller you bought from (checked first on your own Purchases page; your eBay
  name is never sent) or dispute a report. The server is a separate program
  in tools/seller-list-server.
- Local model (off until you turn it on, a one-time download): double-checks
  a listing's identity and the counted results; it can only make the app ask
  you or move a row to "to check" - never add a row or change a price.
- Text from eBay pages and from other users is always shown as plain text.
