# GrowTent v0.5.26

- Fix PM1 discovery when the HTTP transport successfully receives a Switch.GetStatus error response (such as HTTP 404).
- Require HTTP 2xx for successful status samples and PM1/Gen1 fallback responses.
- Match the actual transport contract in the regression test: receiving an HTTP error is still a completed transport request.
