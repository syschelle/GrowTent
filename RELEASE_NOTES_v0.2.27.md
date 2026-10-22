# GrowTent v0.2.27

## Web UI heap stability

This release fixes a condition where the main GrowTent web interface could stop loading even though lightweight endpoints were still reachable.

### Root cause

The main HTML template is larger than 52 KB. Previous versions copied the entire page into one Arduino `String` and then performed many placeholder replacements before sending it to the browser.

On a running ESP32 with fragmented heap, the total free heap could still be sufficient while the largest contiguous free block was only slightly larger than the HTML document. In that state, opening `/` could fail or become extremely slow because the firmware needed one large contiguous allocation for the complete page.

### Changed

- The main web interface is now streamed in newline-aligned chunks of approximately 4 KB.
- Dynamic server-side placeholders are resolved per chunk.
- The firmware no longer requires a single >52 KB contiguous heap allocation to render `/`.
- A failed small chunk allocation is logged as `[WEB] HTML chunk allocation failed`.
- Browser asset cache versions were updated to v0.2.27.
- Shelly status polling is staggered so only one device is checked per step instead of polling all six devices back-to-back.
- The normal per-device refresh remains close to the previous cadence, while unreachable devices can no longer create a long burst of consecutive HTTP timeouts.

### Unchanged

- Shelly Flood S Gen4 remains event-driven and is not polled.
- No additional FreeRTOS task was added.
- Flood IP address and webhook token remain absent from `/api/state`.
- Existing sensor, VPD, irrigation, relay and Shelly control logic is unchanged by this fix.
