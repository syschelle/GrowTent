# GrowTent v0.4.25

## Release summary

GrowTent v0.4.25 improves the responsive behavior of the compact Operating Settings page introduced in the previous releases. The four-card top row is now reserved for genuinely wide displays; Full-HD browser layouts use a cleaner two-by-two arrangement so controls no longer become cramped or overflow their cards.

## Changes

- Uses a 2 × 2 top-card layout on Full-HD and medium desktop browser widths.
- Keeps the 4-column top-card layout for very wide displays only.
- Keeps the existing single-column mobile fallback.
- Stretches cards within each grid row to equal height for a cleaner visual rhythm.
- Prevents the Irrigation Settings content from being squeezed into an unusably narrow card on Full-HD screens.
- Keeps all Operating Settings control IDs, save behavior, API contracts, automation logic, and page-aware polling unchanged.
- Advances firmware and static-asset cache-buster metadata to v0.4.25.

## Compatibility

- No API changes.
- No stored-settings changes.
- No climate, relay, irrigation, Shelly, or OTA behavior changes.
