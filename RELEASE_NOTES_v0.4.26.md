# GrowTent v0.4.26

## Release summary

GrowTent v0.4.26 refines the Full-HD Operating Settings layout again. Instead of switching the top section to a 2 x 2 grid on Full-HD, the four cards remain on one row with asymmetric widths: the compact Temperature card is reduced substantially and the freed space is assigned primarily to Irrigation Settings.

## Changes

- Keeps Grow periods, Temperature, VPD Settings, and Irrigation Settings on one horizontal row on Full-HD desktop layouts.
- Reduces the Temperature card to a compact track because it only contains two controls.
- Gives the Irrigation Settings card the largest share of the row to avoid wrapped or overflowing controls.
- Slightly relaxes the internal Irrigation two-column minimum widths so the Flood option remains inside the card.
- Falls back to a 2 x 2 layout below 1500 px and to the existing single-column mobile layout below 760 px.
- Keeps all control IDs, save behavior, page-aware polling, stored settings, and API contracts unchanged.
- Advances firmware and static-asset cache-buster metadata to v0.4.26.

## Compatibility

- No API changes.
- No stored-settings changes.
- No climate, relay, irrigation, Shelly, or OTA behavior changes.
