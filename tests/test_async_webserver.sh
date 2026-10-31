#!/usr/bin/env bash
# GrowTent AsyncWebServer staged migration test
# Test build: v0.4.22 / phase 32
# Usage: ./tests/test_async_webserver.sh <ESP-IP>
# Example: ./tests/test_async_webserver.sh 192.168.178.58

set -u
export LC_ALL=C
export LANG=C

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <ESP-IP>"
  exit 2
fi

ESP_IP="$1"
TEST_FW_VERSION="v0.4.22"
TEST_CACHE_BUSTER="${TEST_FW_VERSION#v}"
BASE80="http://${ESP_IP}"
BASE8080="http://${ESP_IP}:8080"
STAMP="$(date '+%Y-%m-%d_%H-%M-%S')"
OUTDIR="growtent-async-test-${STAMP}"
LOGFILE="${OUTDIR}/growtent-async-test-${STAMP}.txt"
DATAFILE="${OUTDIR}/requests.tsv"
SUMMARYFILE="${OUTDIR}/summary.txt"

mkdir -p "$OUTDIR"

if ! command -v curl >/dev/null 2>&1; then
  echo "ERROR: curl is required."
  exit 3
fi

log() {
  printf '%s\n' "$*" | tee -a "$LOGFILE"
}

now() {
  date '+%Y-%m-%d %H:%M:%S'
}

printf 'timestamp\tphase\tlabel\thttp\tseconds\tbytes\tcurl_rc\n' > "$DATAFILE"

record_result() {
  local phase="$1" label="$2" result="$3" rc="$4"
  local http seconds bytes
  IFS=$'\t' read -r http seconds bytes <<< "$result"
  http="${http:-000}"
  seconds="${seconds:-0}"
  bytes="${bytes:-0}"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$(date '+%Y-%m-%dT%H:%M:%S')" "$phase" "$label" "$http" "$seconds" "$bytes" "$rc" >> "$DATAFILE"
}

one_request() {
  local phase="$1" label="$2" url="$3" max_time="${4:-5}"
  local result rc
  result="$(curl -sS -o /dev/null --max-time "$max_time" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" 2>>"$LOGFILE")"
  rc=$?
  record_result "$phase" "$label" "$result" "$rc"
  printf '%s %s rc=%s\n' "$label" "$result" "$rc"
}

post_json_capture() {
  local phase="$1" label="$2" url="$3" json_body="$4" bodyfile="$5" max_time="${6:-5}"
  local result rc
  result="$(curl -sS -o "$bodyfile" --max-time "$max_time" \
    -H 'Content-Type: application/json' \
    -X POST --data-binary "$json_body" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" 2>>"$LOGFILE")"
  rc=$?
  record_result "$phase" "$label" "$result" "$rc"
  printf '%s\n' "$result"
  return "$rc"
}

post_empty_capture() {
  local phase="$1" label="$2" url="$3" bodyfile="$4" max_time="${5:-5}"
  local result rc
  result="$(curl -sS -o "$bodyfile" --max-time "$max_time" \
    -X POST \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" 2>>"$LOGFILE")"
  rc=$?
  record_result "$phase" "$label" "$result" "$rc"
  printf '%s\n' "$result"
  return "$rc"
}

parallel_request_to_file() {
  local url="$1" max_time="$2" outfile="$3"
  curl -sS -o /dev/null --max-time "$max_time" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" >"$outfile" 2>>"$LOGFILE"
  printf '%s' "$?" >"${outfile}.rc"
}

collect_parallel() {
  local phase="$1" label="$2" outfile="$3"
  local result rc
  result="$(cat "$outfile" 2>/dev/null || true)"
  rc="$(cat "${outfile}.rc" 2>/dev/null || echo 99)"
  record_result "$phase" "$label" "$result" "$rc"
  rm -f "$outfile" "${outfile}.rc"
}

# Phase-4 diagnostic variant: keep response body/headers/stderr when a burst
# request fails so a sporadic transport/body failure can be identified exactly.
# This is read-only and does not change the actual ESP request load.
parallel_burst_request_to_file() {
  local url="$1" max_time="$2" outfile="$3"
  local body="${outfile}.body" headers="${outfile}.headers" err="${outfile}.stderr"
  curl -sS --http1.1 -D "$headers" -o "$body" --max-time "$max_time" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" >"$outfile" 2>"$err"
  printf '%s' "$?" >"${outfile}.rc"
}

collect_burst_parallel() {
  local phase="$1" label="$2" outfile="$3" round="$4" slot="$5" endpoint="$6"
  local result rc http seconds bytes body headers err actual_bytes ctype first_byte stderr_text
  body="${outfile}.body"
  headers="${outfile}.headers"
  err="${outfile}.stderr"
  result="$(cat "$outfile" 2>/dev/null || true)"
  rc="$(cat "${outfile}.rc" 2>/dev/null || echo 99)"
  IFS=$'\t' read -r http seconds bytes <<< "$result"
  http="${http:-000}"
  seconds="${seconds:-0}"
  bytes="${bytes:-0}"
  actual_bytes="$(wc -c < "$body" 2>/dev/null | tr -d ' ' || echo 0)"
  actual_bytes="${actual_bytes:-0}"
  ctype="$(awk 'BEGIN{IGNORECASE=1} /^Content-Type:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$headers" 2>/dev/null || true)"
  first_byte="$(head -c 1 "$body" 2>/dev/null || true)"
  stderr_text="$(tr '\n' ' ' < "$err" 2>/dev/null | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//' || true)"

  record_result "$phase" "$label" "$result" "$rc"

  if [[ "$rc" -ne 0 || "$http" -lt 200 || "$http" -ge 300 || "$bytes" -lt 5000 || "$actual_bytes" -lt 5000 || "$first_byte" != "{" ]]; then
    log "BURST FAILURE round=$round slot=$slot endpoint=$endpoint HTTP=$http total=${seconds}s rc=$rc reportedBytes=$bytes actualBytes=$actual_bytes contentType='${ctype:-missing}' firstByte='${first_byte:-missing}'"
    if [[ -n "$stderr_text" ]]; then
      log "BURST FAILURE stderr: $stderr_text"
    fi
    log "BURST FAILURE artifacts preserved: $body $headers $err"
  else
    rm -f "$body" "$headers" "$err"
  fi
  rm -f "$outfile" "${outfile}.rc"
}

show_headers() {
  local label="$1" url="$2"
  log "--- HEADERS: $label ---"
  curl -sS --max-time 5 -D - -o /dev/null "$url" 2>&1 \
    | grep -Ei '^(HTTP/|X-GrowTent-WebServer:|X-GrowTent-State-Cache:|X-GrowTent-HTML-Chunk:|X-GrowTent-Endpoint:|X-GrowTent-Request-ID:|X-GrowTent-Client-RID:|X-GrowTent-State-Transport:|X-GrowTent-State-Block-Max:|X-GrowTent-State-Source-Length:|X-GrowTent-State-Format:|X-GrowTent-Asset-Transport:|X-GrowTent-Asset-Block-Max:|Transfer-Encoding:|Cache-Control:|Content-Type:|Content-Length:)' \
    | tee -a "$LOGFILE" || true
}

log "GrowTent AsyncWebServer test"
log "Test build target: v0.4.22 / Async phase 32"
log "Started: $(now)"
log "ESP IP: $ESP_IP"
log "Port 80 in normal STA mode: $BASE80 (must be closed; provisioning owns it only in AP mode)"
log "Port 8080: $BASE8080"
log "Output directory: $OUTDIR"
log "NOTE: An external GrowTent-backend may poll port 8080 during this test; cache/timing counters therefore include real client load."
log "NOTE: Port 8080 is the permanent dashboard/API target. In normal STA mode Phase 32 must have no TCP/80 listener."
log ""

EXPECTED_FW_VERSION="v0.4.22"
EXPECTED_ASYNC_PHASE="32"

log "===== PREFLIGHT: SCRIPT / FIRMWARE COMPATIBILITY ====="
preflight_body="${OUTDIR}/preflight-health.json"
preflight_headers="${OUTDIR}/preflight-health.headers"
preflight_meta="$(curl -sS --http1.1 --max-time 5 -D "$preflight_headers" -o "$preflight_body" \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/api/async/health" 2>>"$LOGFILE")"
preflight_rc=$?
IFS=$'\t' read -r preflight_http preflight_seconds preflight_bytes <<< "$preflight_meta"
preflight_http="${preflight_http:-000}"
health_content_length="$(awk 'BEGIN{IGNORECASE=1} /^Content-Length:/ {gsub("\r",""); print $2; exit}' "$preflight_headers" 2>/dev/null || true)"
health_source_length="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Health-Source-Length:/ {gsub("\r",""); print $2; exit}' "$preflight_headers" 2>/dev/null || true)"
health_transport="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Health-Transport:/ {sub(/^[^:]*:[[:space:]]*/,""); gsub("\r",""); print; exit}' "$preflight_headers" 2>/dev/null || true)"
if [[ "$preflight_rc" -ne 0 || "$preflight_http" != "200" || "$health_transport" != "direct-buffer-fixed-length" || -z "$health_content_length" || -z "$health_source_length" || "$health_content_length" != "$health_source_length" || "$preflight_bytes" != "$health_content_length" ]]; then
  log "ABORT firmware compatibility check: /api/async/health transport invalid (HTTP=$preflight_http rc=$preflight_rc bytes=${preflight_bytes:-0} contentLength=${health_content_length:-missing} sourceLength=${health_source_length:-missing} transport=${health_transport:-missing})."
  log "No write, hardware-action, legacy-port, asset-stress or load test has been executed."
  exit 20
fi
preflight_json="$(cat "$preflight_body" 2>/dev/null || true)"
observed_fw="$(printf '%s' "$preflight_json" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')"
observed_phase="$(printf '%s' "$preflight_json" | sed -n 's/.*"phase":\([0-9][0-9]*\).*/\1/p')"
if [[ "$observed_fw" != "$EXPECTED_FW_VERSION" || "$observed_phase" != "$EXPECTED_ASYNC_PHASE" ]]; then
  log "ABORT script/firmware mismatch: script expects firmware=$EXPECTED_FW_VERSION phase=$EXPECTED_ASYNC_PHASE; ESP reports firmware=${observed_fw:-missing} phase=${observed_phase:-missing}."
  log "Use the test script shipped with the exact flashed firmware package."
  log "No write, hardware-action, legacy-port, asset-stress or load test has been executed."
  exit 21
fi
log "PASS script/firmware compatibility: firmware=$observed_fw phase=$observed_phase"
rm -f "$preflight_body" "$preflight_headers"
log ""

log "===== PHASE 0: CONNECTIVITY / ENDPOINT SMOKE TEST ====="
show_headers "async root" "$BASE8080/"
show_headers "async state" "$BASE8080/api/state"
log ""

LEGACY_UI_FAILURES=0
probe_port80_closed() {
  local label="$1" path="$2" method="${3:-GET}"
  local body="${OUTDIR}/port80-${label}.body" meta rc http seconds bytes
  if [[ "$method" == "POST" ]]; then
    meta="$(curl -sS --http1.1 --max-time 2 -o "$body" -X POST \
      -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE80$path" 2>>"$LOGFILE")"
  else
    meta="$(curl -sS --http1.1 --max-time 2 -o "$body" \
      -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE80$path" 2>>"$LOGFILE")"
  fi
  rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"
  http="${http:-000}"; seconds="${seconds:-0}"; bytes="${bytes:-0}"
  record_result "port80-closed" "$label" "$meta" "$rc"
  rm -f "$body"
  if [[ "$rc" -ne 0 && "$http" == "000" ]]; then
    log "PASS normal-STA TCP/80 closed $label: HTTP=000 rc=$rc"
    return 0
  fi
  log "FAIL normal-STA TCP/80 unexpectedly reachable $label: HTTP=$http rc=$rc bytes=$bytes"
  return 1
}

log "===== PHASE 0A: PHASE-32 NORMAL-STA TCP/80 LISTENER REMOVAL ====="
for spec in "root|/|GET" "style|/style.css|GET" "script|/script.js|GET" "favicon|/favicon.ico|GET" "saverunsettings|/saverunsettings|POST"; do
  IFS='|' read -r label path method <<< "$spec"
  if ! probe_port80_closed "$label" "$path" "$method"; then
    LEGACY_UI_FAILURES=$((LEGACY_UI_FAILURES + 1))
  fi
done
# v0.4.17: when STA is connected, a boot-time fallback AP must already have
# released DNS/TCP80 and returned to STA-only mode. This check catches the
# v0.3.08 lifecycle bug directly instead of relying only on port probes.
phase0a_health="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/async/health" 2>>"$LOGFILE" || true)"
if [[ "$phase0a_health" == *'"wifiStatus":3'* && \
      "$phase0a_health" == *'"apProvisioningActive":false'* && \
      "$phase0a_health" == *'"apProvisioningServerStarted":false'* && \
      "$phase0a_health" == *'"apProvisioningFallbackFromStoredCredentials":false'* && \
      "$phase0a_health" == *'"apFallbackAutoStopEnabled":true'* ]]; then
  log "PASS normal-STA provisioning lifecycle: AP/DNS/TCP80 inactive after STA recovery"
else
  log "FAIL normal-STA provisioning lifecycle health: $phase0a_health"
  LEGACY_UI_FAILURES=$((LEGACY_UI_FAILURES + 1))
fi
if [[ "$LEGACY_UI_FAILURES" -eq 0 ]]; then
  log "NORMAL-STA TCP/80 + PROVISIONING LIFECYCLE: PASS"
else
  log "NORMAL-STA TCP/80 + PROVISIONING LIFECYCLE: FAIL ($LEGACY_UI_FAILURES problem(s))"
fi
log ""

ASSET_FAILURES=0
check_asset() {
  local label="$1" url="$2" min_bytes="$3"
  local tmp="${OUTDIR}/asset-${label}.tmp"
  local meta rc http seconds bytes
  meta="$(curl -sS --max-time 10 -o "$tmp" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" 2>>"$LOGFILE")"
  rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"
  http="${http:-000}"
  bytes="${bytes:-0}"
  record_result "asset-check" "$label" "$meta" "$rc"

  if [[ "$rc" -ne 0 || "$http" != "200" || "$bytes" -lt "$min_bytes" ]]; then
    log "FAIL asset $label: HTTP=$http bytes=$bytes minimum=$min_bytes rc=$rc"
    ASSET_FAILURES=$((ASSET_FAILURES + 1))
  else
    log "PASS asset $label: HTTP=$http bytes=$bytes minimum=$min_bytes"
  fi
  rm -f "$tmp"
}

log "===== PHASE 0B: STATIC ASSET INTEGRITY ====="
check_asset "async-root" "$BASE8080/" 45000
check_asset "async-css" "$BASE8080/style.css?v=${TEST_CACHE_BUSTER}" 19000
check_asset "async-js" "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}" 100000
if [[ "$ASSET_FAILURES" -eq 0 ]]; then
  log "STATIC ASSET CHECK: PASS"
else
  log "STATIC ASSET CHECK: FAIL ($ASSET_FAILURES problem(s))"
fi

relay_ui_js="$(curl -sS --max-time 10 "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&relay-ui-ack=1" 2>>"$LOGFILE" || true)"
if [[ "$relay_ui_js" == *"waitForRelayApply"* && "$relay_ui_js" == *"refreshRelayStatesDirect"* && "$relay_ui_js" == *"Manual relays 1..5 are deliberately excluded"* && "$relay_ui_js" == *"sensorRefreshQueued"* ]]; then
  log "PASS relay dashboard uses direct runtime state ownership and stale-refresh suppression"
else
  log "FAIL relay dashboard direct-state ownership/race markers missing/stale"
  ASSET_FAILURES=$((ASSET_FAILURES + 1))
fi
if [[ "$relay_ui_js" == *"/api/settings/run-profile"* && "$relay_ui_js" == *"/api/settings/irrigation"* && "$relay_ui_js" != *'fetch("/saverunsettings"'* ]]; then
  log "PASS Betriebseinstellungen dashboard uses split permanent-port save endpoints"
else
  log "FAIL Betriebseinstellungen split-save markers missing or legacy /saverunsettings fetch remains"
  ASSET_FAILURES=$((ASSET_FAILURES + 1))
fi
if [[ "$relay_ui_js" == *"if (v === '2') return 'flower';"* && "$relay_ui_js" == *"if (v === '3') return 'dry';"* && "$relay_ui_js" != *"v = '%PHASE%'"* ]]; then
  log "PASS dashboard phase selector uses numeric option values consistently"
else
  log "FAIL dashboard phase selector still mixes numeric and textual phase values"
  ASSET_FAILURES=$((ASSET_FAILURES + 1))
fi
if [[ "$relay_ui_js" == *"const heatingRelay = (heatingSource === 1) ? heatingRelayRaw : 0;"* && \
      "$relay_ui_js" == *"sourceType: heatingSource"* && "$relay_ui_js" == *"relay: heatingRelay"* ]]; then
  log "PASS dashboard heating payload canonicalizes empty/non-ESP relay selection to relay=0"
else
  log "FAIL dashboard heating payload can still serialize an empty relay as null"
  ASSET_FAILURES=$((ASSET_FAILURES + 1))
fi
if [[ "$relay_ui_js" == *"function tempCToDisplay"* && \
      "$relay_ui_js" == *"function tempDeltaCToDisplay"* && \
      "$relay_ui_js" == *"function tempDisplayToC"* && \
      "$relay_ui_js" == *"function tempDeltaDisplayToC"* && \
      "$relay_ui_js" == *"targetTemperature: targetTemperatureC"* && \
      "$relay_ui_js" == *"offsetLeafTemperature: offsetLeafTemperatureC"* ]]; then
  log "PASS dashboard temperature-unit contract keeps API Celsius-native and converts absolute/delta values in UI"
else
  log "FAIL dashboard temperature-unit conversion/API contract markers missing"
  ASSET_FAILURES=$((ASSET_FAILURES + 1))
fi
log ""

CACHE_FAILURES=0
cache_header_value() {
  local url="$1"
  curl -sS --max-time 10 -D - -o /dev/null "$url" 2>>"$LOGFILE" \
    | awk 'BEGIN{IGNORECASE=1} /^Cache-Control:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}'
}

check_cache_header() {
  local label="$1" url="$2" expected="$3"
  local value
  value="$(cache_header_value "$url")"
  if [[ "$value" == "$expected" ]]; then
    log "PASS cache $label: $value"
  else
    log "FAIL cache $label: got='${value:-<missing>}' expected='$expected'"
    CACHE_FAILURES=$((CACHE_FAILURES + 1))
  fi
}

log "===== PHASE 0C: CACHE HEADER POLICY ====="
check_cache_header "async-root" "$BASE8080/" "no-store"
check_cache_header "async-css" "$BASE8080/style.css?v=${TEST_CACHE_BUSTER}" "public, max-age=31536000, immutable"
check_cache_header "async-js" "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}" "public, max-age=31536000, immutable"
if [[ "$CACHE_FAILURES" -eq 0 ]]; then
  log "CACHE HEADER CHECK: PASS"
else
  log "CACHE HEADER CHECK: FAIL ($CACHE_FAILURES problem(s))"
fi
log ""

READCACHE_FAILURES=0
state_cache_header_to_file() {
  local outfile="$1"
  curl -sS --max-time 5 -D - -o /dev/null "$BASE8080/api/state" 2>>"$LOGFILE" \
    | awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-State-Cache:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' > "$outfile"
}

log "===== PHASE 0D: ASYNC STATE CACHE BEHAVIOUR ====="
log "Robust check: after expiry, six rapid parallel reads must produce at least one cache hit and no fallback."
sleep 2
cache_files=()
cache_pids=()
for n in $(seq 1 6); do
  f="${OUTDIR}/cache-header-${n}.txt"
  state_cache_header_to_file "$f" & cache_pids+=("$!")
  cache_files+=("$f")
done
for pid in "${cache_pids[@]}"; do wait "$pid" 2>/dev/null || true; done
cache_hits=0
cache_misses=0
cache_fallbacks=0
cache_sequence=""
for f in "${cache_files[@]}"; do
  v="$(cat "$f" 2>/dev/null || true)"
  [[ -n "$cache_sequence" ]] && cache_sequence+=","
  cache_sequence+="${v:-missing}"
  case "$v" in
    hit) cache_hits=$((cache_hits + 1)) ;;
    miss) cache_misses=$((cache_misses + 1)) ;;
    fallback) cache_fallbacks=$((cache_fallbacks + 1)) ;;
  esac
done
rm -f "${cache_files[@]}"
log "Observed cache headers: $cache_sequence"
if [[ "$cache_hits" -ge 1 && "$cache_fallbacks" -eq 0 ]]; then
  log "PASS state cache concurrent behaviour: hits=$cache_hits misses=$cache_misses fallbacks=$cache_fallbacks"
else
  log "FAIL state cache concurrent behaviour: hits=$cache_hits misses=$cache_misses fallbacks=$cache_fallbacks"
  READCACHE_FAILURES=$((READCACHE_FAILURES + 1))
fi
root_chunk="$(curl -sS --max-time 5 -D - -o /dev/null "$BASE8080/" 2>>"$LOGFILE" \
  | awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-HTML-Chunk:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}')"
if [[ "$root_chunk" == "6144" ]]; then
  log "PASS HTML stream chunk header: $root_chunk"
else
  log "FAIL HTML stream chunk header: got='${root_chunk:-<missing>}' expected='6144'"
  READCACHE_FAILURES=$((READCACHE_FAILURES + 1))
fi
if [[ "$READCACHE_FAILURES" -eq 0 ]]; then
  log "ASYNC READ CACHE CHECK: PASS"
else
  log "ASYNC READ CACHE CHECK: FAIL ($READCACHE_FAILURES problem(s))"
fi
log ""

# Phase 25 transport finalization: exercise the native fixed-length
# byte-buffer asset path aggressively. This guards both the old chunked 13-byte
# tail stall and the v0.2.75 callback-response 5-byte tail stall seen during a
# mixed cold-dashboard load. The external GrowTent-backend may continue polling.
ASSET_TRANSPORT_FAILURES=0
ASSET_TRANSPORT_TIMEOUTS=0
ASSET_TRANSPORT_INCOMPLETE=0
ASSET_TRANSPORT_HEADER_ERRORS=0
ASSET_TRANSPORT_SLOW=0
ASSET_TRANSPORT_MAX_TIME=10

asset_transport_request_to_file() {
  local url="$1" outfile="$2" max_time="${3:-$ASSET_TRANSPORT_MAX_TIME}"
  local body="${outfile}.body" headers="${outfile}.headers"
  local result rc
  result="$(curl -sS --http1.1 --max-time "$max_time" -D "$headers" -o "$body" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' "$url" 2>>"$LOGFILE")"
  rc=$?
  printf '%s' "$result" >"$outfile"
  printf '%s' "$rc" >"${outfile}.rc"
}

collect_asset_transport() {
  local label="$1" outfile="$2" expected_bytes="$3" expected_sha="$4"
  local result rc http seconds reported_bytes actual_bytes transport block transfer content_length actual_sha
  result="$(cat "$outfile" 2>/dev/null || true)"
  rc="$(cat "${outfile}.rc" 2>/dev/null || echo 99)"
  IFS=$'\t' read -r http seconds reported_bytes <<< "$result"
  http="${http:-000}"; seconds="${seconds:-0}"; reported_bytes="${reported_bytes:-0}"
  actual_bytes="$(wc -c <"${outfile}.body" 2>/dev/null || echo 0)"
  transport="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Asset-Transport:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "${outfile}.headers" 2>/dev/null || true)"
  block="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Asset-Block-Max:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "${outfile}.headers" 2>/dev/null || true)"
  transfer="$(awk 'BEGIN{IGNORECASE=1} /^Transfer-Encoding:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "${outfile}.headers" 2>/dev/null || true)"
  content_length="$(awk 'BEGIN{IGNORECASE=1} /^Content-Length:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "${outfile}.headers" 2>/dev/null || true)"
  actual_sha="$(sha256sum "${outfile}.body" 2>/dev/null | awk '{print $1}')"

  record_result "asset-transport" "$label" "$result" "$rc"

  local bad=0
  if [[ "$rc" -ne 0 || "$http" != "200" ]]; then
    ASSET_TRANSPORT_FAILURES=$((ASSET_TRANSPORT_FAILURES + 1)); bad=1
    [[ "$rc" -eq 28 ]] && ASSET_TRANSPORT_TIMEOUTS=$((ASSET_TRANSPORT_TIMEOUTS + 1))
  fi
  if [[ "$actual_bytes" -ne "$expected_bytes" || "$reported_bytes" -ne "$expected_bytes" ]]; then
    ASSET_TRANSPORT_INCOMPLETE=$((ASSET_TRANSPORT_INCOMPLETE + 1)); bad=1
  fi
  if [[ "$transport" != "direct-buffer-fixed-length" || -n "$transfer" || "$content_length" != "$expected_bytes" ]]; then
    ASSET_TRANSPORT_HEADER_ERRORS=$((ASSET_TRANSPORT_HEADER_ERRORS + 1)); bad=1
  fi
  if [[ "$actual_sha" != "$expected_sha" ]]; then
    ASSET_TRANSPORT_INCOMPLETE=$((ASSET_TRANSPORT_INCOMPLETE + 1)); bad=1
  fi
  if awk -v t="$seconds" 'BEGIN{exit !(t >= 3.0)}'; then
    ASSET_TRANSPORT_SLOW=$((ASSET_TRANSPORT_SLOW + 1))
  fi
  if [[ "$bad" -ne 0 ]]; then
    log "ASSET EVENT $label HTTP=$http total=${seconds}s rc=$rc bytes=$actual_bytes/$expected_bytes reported=$reported_bytes sha=${actual_sha:-missing}/${expected_sha} transport='${transport:-missing}' block='${block:-missing}' contentLength='${content_length:-missing}' transfer='${transfer:-none}'"
  fi

  rm -f "$outfile" "${outfile}.rc" "${outfile}.body" "${outfile}.headers"
}

log "===== PHASE 0E: STATIC ASSET DIRECT-BUFFER FIXED-LENGTH TRANSPORT REGRESSION ====="
log "Stress: 10 rounds x (4 JS + 2 CSS) concurrent downloads; exact length + SHA-256; external backend polling may continue."
log "Transport correctness hard-timeout: ${ASSET_TRANSPORT_MAX_TIME}s; transfers >=3s are still counted as slow diagnostics."

# Build the integrity baseline from the assets served by the firmware under test.
# This makes Phase 0E independent of firmware-specific JS/CSS sizes while still
# detecting truncation, corruption, inconsistent Content-Length, chunked
# transport regressions and body changes during the concurrent stress run.
phase0e_baseline_dir="${OUTDIR}/phase0e-baseline"
mkdir -p "$phase0e_baseline_dir"

phase0e_capture_baseline() {
  local label="$1" url="$2" body="$3" headers="$4"
  local meta rc http reported_bytes actual_bytes content_length transfer transport sha

  meta="$(curl -sS --http1.1 --max-time "$ASSET_TRANSPORT_MAX_TIME" \
    -D "$headers" -o "$body" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' \
    "$url" 2>>"$LOGFILE")"
  rc=$?

  IFS=$'\t' read -r http _ reported_bytes <<< "$meta"
  http="${http:-000}"
  reported_bytes="${reported_bytes:-0}"
  actual_bytes="$(wc -c <"$body" 2>/dev/null || echo 0)"
  content_length="$(awk 'tolower($1)=="content-length:" {gsub("\r","",$2); v=$2} END{print v}' "$headers" 2>/dev/null || true)"
  transfer="$(awk 'tolower($1)=="transfer-encoding:" {gsub("\r","",$2); v=$2} END{print v}' "$headers" 2>/dev/null || true)"
  transport="$(awk 'tolower($1)=="x-growtent-asset-transport:" {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); v=$0} END{print v}' "$headers" 2>/dev/null || true)"
  sha="$(sha256sum "$body" 2>/dev/null | awk '{print $1}')"

  if [[ "$rc" -ne 0 || "$http" != "200" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: HTTP=$http rc=$rc"
    return 1
  fi
  if [[ "$actual_bytes" -le 0 || "$reported_bytes" -ne "$actual_bytes" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: body=$actual_bytes reported=$reported_bytes"
    return 1
  fi
  if [[ -z "$content_length" || "$content_length" -ne "$actual_bytes" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: Content-Length='${content_length:-missing}' body=$actual_bytes"
    return 1
  fi
  if [[ -n "$transfer" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: unexpected Transfer-Encoding='$transfer'"
    return 1
  fi
  if [[ "$transport" != "direct-buffer-fixed-length" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: transport='${transport:-missing}'"
    return 1
  fi
  if [[ -z "$sha" ]]; then
    log "FAIL Phase 0E dynamic baseline $label: SHA-256 unavailable"
    return 1
  fi

  printf '%s\t%s\n' "$actual_bytes" "$sha"
  return 0
}

phase0e_js_body="${phase0e_baseline_dir}/script.js"
phase0e_js_headers="${phase0e_baseline_dir}/script.headers"
phase0e_css_body="${phase0e_baseline_dir}/style.css"
phase0e_css_headers="${phase0e_baseline_dir}/style.headers"

if ! phase0e_js_ref="$(phase0e_capture_baseline \
    "script.js" \
    "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&phase0e=baseline" \
    "$phase0e_js_body" "$phase0e_js_headers")"; then
  log "STATIC ASSET DIRECT-BUFFER FIXED-LENGTH TRANSPORT: FAIL (dynamic JS baseline)"
  exit 1
fi

if ! phase0e_css_ref="$(phase0e_capture_baseline \
    "style.css" \
    "$BASE8080/style.css?v=${TEST_CACHE_BUSTER}&phase0e=baseline" \
    "$phase0e_css_body" "$phase0e_css_headers")"; then
  log "STATIC ASSET DIRECT-BUFFER FIXED-LENGTH TRANSPORT: FAIL (dynamic CSS baseline)"
  exit 1
fi

IFS=$'\t' read -r EXPECTED_JS_BYTES EXPECTED_JS_SHA256 <<< "$phase0e_js_ref"
IFS=$'\t' read -r EXPECTED_CSS_BYTES EXPECTED_CSS_SHA256 <<< "$phase0e_css_ref"

log "PASS Phase 0E dynamic baseline: script.js bytes=$EXPECTED_JS_BYTES sha256=$EXPECTED_JS_SHA256"
log "PASS Phase 0E dynamic baseline: style.css bytes=$EXPECTED_CSS_BYTES sha256=$EXPECTED_CSS_SHA256"

asset_baseline_headers="${OUTDIR}/asset-transport-baseline.headers"
asset_baseline_body="${OUTDIR}/asset-transport-baseline.js"
asset_baseline_meta="$(curl -sS --http1.1 --max-time "$ASSET_TRANSPORT_MAX_TIME" -D "$asset_baseline_headers" -o "$asset_baseline_body" \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&phase16=baseline" 2>>"$LOGFILE")"
asset_baseline_rc=$?
IFS=$'\t' read -r asset_baseline_http asset_baseline_seconds asset_baseline_bytes <<< "$asset_baseline_meta"
asset_baseline_actual="$(wc -c <"$asset_baseline_body" 2>/dev/null || echo 0)"
asset_baseline_transport="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Asset-Transport:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$asset_baseline_headers" 2>/dev/null || true)"
asset_baseline_block="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Asset-Block-Max:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$asset_baseline_headers" 2>/dev/null || true)"
asset_baseline_te="$(awk 'BEGIN{IGNORECASE=1} /^Transfer-Encoding:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$asset_baseline_headers" 2>/dev/null || true)"
asset_baseline_cl="$(awk 'BEGIN{IGNORECASE=1} /^Content-Length:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$asset_baseline_headers" 2>/dev/null || true)"
asset_baseline_sha="$(sha256sum "$asset_baseline_body" 2>/dev/null | awk '{print $1}')"
record_result "asset-transport" "async-js-baseline" "$asset_baseline_meta" "$asset_baseline_rc"
if [[ "$asset_baseline_rc" -eq 0 && "$asset_baseline_http" == "200" && "$asset_baseline_actual" -eq "$EXPECTED_JS_BYTES" && "$asset_baseline_bytes" -eq "$EXPECTED_JS_BYTES" && "$asset_baseline_sha" == "$EXPECTED_JS_SHA256" && "$asset_baseline_transport" == "direct-buffer-fixed-length" && "$asset_baseline_cl" == "$EXPECTED_JS_BYTES" && -z "$asset_baseline_te" ]]; then
  log "PASS asset transport baseline: HTTP=$asset_baseline_http bytes=$asset_baseline_actual sha=$asset_baseline_sha transport=$asset_baseline_transport contentLength=$asset_baseline_cl"
else
  log "FAIL asset transport baseline: HTTP=${asset_baseline_http:-000} bytes=$asset_baseline_actual/$EXPECTED_JS_BYTES rc=$asset_baseline_rc sha='${asset_baseline_sha:-missing}' transport='${asset_baseline_transport:-missing}' block='${asset_baseline_block:-none}' contentLength='${asset_baseline_cl:-missing}' transfer='${asset_baseline_te:-none}'"
  ASSET_TRANSPORT_FAILURES=$((ASSET_TRANSPORT_FAILURES + 1))
fi
rm -f "$asset_baseline_headers" "$asset_baseline_body"

for round in $(seq 1 10); do
  pids=(); files=(); labels=(); expects=(); hashes=()
  for n in $(seq 1 4); do
    f="${OUTDIR}/asset16-r${round}-js${n}.tmp"
    asset_transport_request_to_file "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&phase16=${round}-${n}" "$f" "$ASSET_TRANSPORT_MAX_TIME" & pids+=("$!")
    files+=("$f"); labels+=("async-js-flow"); expects+=("$EXPECTED_JS_BYTES"); hashes+=("$EXPECTED_JS_SHA256")
  done
  for n in $(seq 1 2); do
    f="${OUTDIR}/asset16-r${round}-css${n}.tmp"
    asset_transport_request_to_file "$BASE8080/style.css?v=${TEST_CACHE_BUSTER}&phase16=${round}-${n}" "$f" "$ASSET_TRANSPORT_MAX_TIME" & pids+=("$!")
    files+=("$f"); labels+=("async-css-flow"); expects+=("$EXPECTED_CSS_BYTES"); hashes+=("$EXPECTED_CSS_SHA256")
  done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
  for idx in "${!files[@]}"; do
    collect_asset_transport "${labels[$idx]}" "${files[$idx]}" "${expects[$idx]}" "${hashes[$idx]}"
  done
  log "asset transport progress: $round/10 rounds; failures=$ASSET_TRANSPORT_FAILURES timeouts=$ASSET_TRANSPORT_TIMEOUTS incomplete=$ASSET_TRANSPORT_INCOMPLETE headerErrors=$ASSET_TRANSPORT_HEADER_ERRORS slow=$ASSET_TRANSPORT_SLOW"
  sleep 0.25
done

if [[ "$ASSET_TRANSPORT_FAILURES" -eq 0 && "$ASSET_TRANSPORT_TIMEOUTS" -eq 0 && "$ASSET_TRANSPORT_INCOMPLETE" -eq 0 && "$ASSET_TRANSPORT_HEADER_ERRORS" -eq 0 ]]; then
  log "STATIC ASSET DIRECT-BUFFER FIXED-LENGTH TRANSPORT: PASS (JS=40 CSS=20 timeouts=0 incomplete=0 headerErrors=0 slow=$ASSET_TRANSPORT_SLOW)"
else
  log "STATIC ASSET DIRECT-BUFFER FIXED-LENGTH TRANSPORT: FAIL (failures=$ASSET_TRANSPORT_FAILURES timeouts=$ASSET_TRANSPORT_TIMEOUTS incomplete=$ASSET_TRANSPORT_INCOMPLETE headerErrors=$ASSET_TRANSPORT_HEADER_ERRORS slow=$ASSET_TRANSPORT_SLOW)"
fi
log ""

PREFWRITE_FAILURES=0
log "===== PHASE 0F: PERSISTENT ASYNC SYSTEM SETTINGS (INCLUDING RESTART-APPLIED DS18/RELAY COUNT) ====="

json_string_field() {
  local json="$1" field="$2"
  printf '%s' "$json" | sed -n "s/.*\"${field}\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p"
}

json_uint_field() {
  local json="$1" field="$2"
  printf '%s' "$json" | sed -n "s/.*\"${field}\":\([0-9][0-9]*\).*/\1/p"
}

json_bool_field() {
  local json="$1" field="$2"
  printf '%s' "$json" | sed -n "s/.*\"${field}\":\(true\|false\).*/\1/p"
}

json_number_field() {
  local json="$1" field="$2"
  printf '%s' "$json" | sed -n "s/.*\"${field}\"[[:space:]]*:[[:space:]]*\(-\{0,1\}[0-9][0-9]*\(\.[0-9][0-9]*\)\{0,1\}\).*/\1/p"
}

json_nested_number_field() {
  local json="$1" object="$2" field="$3"
  printf '%s' "$json" | sed -n "s/.*\"${object}\":{[^}]*\"${field}\":\(-\{0,1\}[0-9][0-9]*\(\.[0-9][0-9]*\)\{0,1\}\).*/\1/p"
}

json_nested_bool_field() {
  local json="$1" object="$2" field="$3"
  printf '%s' "$json" | sed -n "s/.*\"${object}\":{[^}]*\"${field}\":\(true\|false\).*/\1/p"
}

json_nested_string_field() {
  local json="$1" object="$2" field="$3"
  printf '%s' "$json" | sed -n "s/.*\"${object}\":{[^}]*\"${field}\":\"\([^\"]*\)\".*/\1/p"
}

json_nested2_string_field() {
  local json="$1" object="$2" child="$3" field="$4"
  printf '%s' "$json" | sed -n "s/.*\"${object}\":{.*\"${child}\":{[^}]*\"${field}\":\"\([^\"]*\)\".*/\1/p"
}

json_nested2_number_field() {
  local json="$1" object="$2" child="$3" field="$4"
  printf '%s' "$json" | sed -n "s/.*\"${object}\":{.*\"${child}\":{[^}]*\"${field}\":\(-\{0,1\}[0-9][0-9]*\(\.[0-9][0-9]*\)\{0,1\}\).*/\1/p"
}

float_close() {
  awk -v a="$1" -v b="$2" -v eps="${3:-0.005}" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(d<=eps) }'
}

json_escape_string() {
  local v="$1"
  v="${v//\\/\\\\}"
  v="${v//\"/\\\"}"
  printf '%s' "$v"
}

# Health/counter reads are diagnostic-critical. A single transient empty or
# truncated response must not be silently converted into zero counters. Retry
# and require a complete-looking health JSON before returning it.
fetch_async_health() {
  local attempts="${1:-5}" i body rc
  for i in $(seq 1 "$attempts"); do
    body="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/async/health" 2>>"$LOGFILE")"
    rc=$?
    if [[ "$rc" -eq 0 && "$body" == \{* && "$body" == *\} && "$body" == *'"ok":true'* && "$body" == *'"version":'* && "$body" == *'"stateTransport":'* ]]; then
      printf '%s' "$body"
      return 0
    fi
    printf '%s WARN async-health read attempt %d/%d failed or incomplete: rc=%s bytes=%s\n' \
      "$(now)" "$i" "$attempts" "$rc" "${#body}" >>"$LOGFILE"
    sleep 0.10
  done
  return 1
}

test_persistent_string_setting() {
  local label="$1" path="$2" json_field="$3" test_value="$4" invalid_value="$5" invalid_error="$6" request_counter="$7" failure_counter="$8"
  local before_json original runtime health_before count_before fail_before
  local write_body write_meta write_rc write_http write_seconds write_bytes write_response
  local verify_json verify_stored bad_body bad_meta bad_rc bad_http bad_seconds bad_bytes bad_response
  local restore_body restore_meta restore_rc restore_http restore_seconds restore_bytes restore_response final_json final_stored
  local health_after count_after fail_after escaped_test escaped_invalid escaped_original escaped_runtime

  before_json="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  original="$(json_string_field "$before_json" stored)"
  runtime="$(json_string_field "$before_json" runtime)"
  if [[ "$before_json" != *'"stored":"'* ]]; then
    log "FAIL $label persistent pre-read: response='$before_json'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
    return
  fi

  health_before="$(fetch_async_health 5 || true)"
  count_before="$(json_uint_field "$health_before" "$request_counter")"
  fail_before="$(json_uint_field "$health_before" "$failure_counter")"
  count_before="${count_before:-0}"
  fail_before="${fail_before:-0}"

  log "$label original stored='$original' runtime='$runtime'; temporary test value='$test_value'"
  escaped_test="$(json_escape_string "$test_value")"
  escaped_invalid="$(json_escape_string "$invalid_value")"
  escaped_original="$(json_escape_string "$original")"
  escaped_runtime="$(json_escape_string "$runtime")"

  write_body="${OUTDIR}/${label}-write-test.json"
  write_meta="$(post_json_capture "prefs-write" "async-${label}-write" \
    "$BASE8080$path" "{\"$json_field\":\"$escaped_test\"}" "$write_body" 5)"
  write_rc=$?
  IFS=$'\t' read -r write_http write_seconds write_bytes <<< "$write_meta"
  write_response="$(cat "$write_body" 2>/dev/null || true)"
  if [[ "$write_rc" -eq 0 && "$write_http" == "200" && "$write_response" == *"\"stored\":\"$escaped_test\""* && "$write_response" == *"\"runtime\":\"$escaped_runtime\""* ]]; then
    log "PASS $label persistent write: HTTP=$write_http response=$write_response"
  else
    log "FAIL $label persistent write: HTTP=${write_http:-000} rc=$write_rc response='${write_response:-<empty>}'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  fi

  verify_json="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  verify_stored="$(json_string_field "$verify_json" stored)"
  if [[ "$verify_stored" == "$test_value" ]]; then
    log "PASS $label NVS readback after write: stored='$verify_stored'"
  else
    log "FAIL $label NVS readback after write: got='${verify_stored:-<missing>}' expected='$test_value' response='$verify_json'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  fi

  bad_body="${OUTDIR}/${label}-write-invalid.json"
  bad_meta="$(post_json_capture "negative-test" "async-${label}-invalid" \
    "$BASE8080$path" "{\"$json_field\":\"$escaped_invalid\"}" "$bad_body" 5)"
  bad_rc=$?
  IFS=$'\t' read -r bad_http bad_seconds bad_bytes <<< "$bad_meta"
  bad_response="$(cat "$bad_body" 2>/dev/null || true)"
  if [[ "$bad_rc" -eq 0 && "$bad_http" == "400" && "$bad_response" == *"\"$invalid_error\""* ]]; then
    log "PASS $label invalid value rejected: HTTP=400 response=$bad_response"
  else
    log "FAIL $label invalid value handling: HTTP=${bad_http:-000} rc=$bad_rc response='${bad_response:-<empty>}'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  fi

  restore_body="${OUTDIR}/${label}-write-restore.json"
  restore_meta="$(post_json_capture "prefs-restore" "async-${label}-restore" \
    "$BASE8080$path" "{\"$json_field\":\"$escaped_original\"}" "$restore_body" 5)"
  restore_rc=$?
  IFS=$'\t' read -r restore_http restore_seconds restore_bytes <<< "$restore_meta"
  final_json="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  final_stored="$(json_string_field "$final_json" stored)"
  if [[ "$restore_rc" -eq 0 && "$restore_http" == "200" && "$final_stored" == "$original" ]]; then
    log "PASS $label original persistent value restored: stored='$final_stored'"
  else
    log "FAIL CRITICAL $label restore: HTTP=${restore_http:-000} rc=$restore_rc finalStored='${final_stored:-<missing>}' original='$original'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  fi

  health_after="$(fetch_async_health 5 || true)"
  count_after="$(json_uint_field "$health_after" "$request_counter")"
  fail_after="$(json_uint_field "$health_after" "$failure_counter")"
  count_after="${count_after:-0}"
  fail_after="${fail_after:-0}"
  if [[ "$count_after" -eq $((count_before + 2)) && "$fail_after" -eq "$fail_before" ]]; then
    log "PASS $label counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"
  else
    log "FAIL $label counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  fi
}

test_persistent_bool_setting() {
  local label="$1" path="$2" json_field="$3" request_counter="$4" failure_counter="$5" invalid_error="${6:-invalid_debug}"
  local before original runtime test_value health_before count_before fail_before meta rc http seconds bytes response verify final
  before="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  original="$(json_bool_field "$before" stored)"; runtime="$(json_bool_field "$before" runtime)"
  if [[ "$original" != "true" && "$original" != "false" ]]; then log "FAIL $label pre-read: $before"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); return; fi
  [[ "$original" == "true" ]] && test_value=false || test_value=true
  health_before="$(fetch_async_health 5 || true)"
  count_before="$(json_uint_field "$health_before" "$request_counter")"; fail_before="$(json_uint_field "$health_before" "$failure_counter")"
  count_before="${count_before:-0}"; fail_before="${fail_before:-0}"
  log "$label original stored=$original runtime=$runtime; temporary test value=$test_value"
  meta="$(post_json_capture "prefs-write" "async-${label}-write" "$BASE8080$path" "{\"$json_field\":$test_value}" "${OUTDIR}/${label}-write.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/${label}-write.json" 2>/dev/null || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$response" == *"\"stored\":$test_value"* && "$response" == *"\"runtime\":$runtime"* ]]; then log "PASS $label persistent write with runtime unchanged: $response"; else log "FAIL $label persistent write/runtime invariant: HTTP=${http:-000} rc=$rc response='$response' runtime=$runtime"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); fi
  verify="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  [[ "$(json_bool_field "$verify" stored)" == "$test_value" ]] && log "PASS $label NVS readback after write" || { log "FAIL $label NVS readback: $verify"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "negative-test" "async-${label}-invalid" "$BASE8080$path" "{\"$json_field\":\"yes\"}" "${OUTDIR}/${label}-invalid.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/${label}-invalid.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *"\"$invalid_error\""* ]] && log "PASS $label invalid value rejected" || { log "FAIL $label invalid value handling: HTTP=${http:-000} response='$response' expected=$invalid_error"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "prefs-restore" "async-${label}-restore" "$BASE8080$path" "{\"$json_field\":$original}" "${OUTDIR}/${label}-restore.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"
  final="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  [[ "$rc" -eq 0 && "$http" == "200" && "$(json_bool_field "$final" stored)" == "$original" ]] && log "PASS $label original persistent value restored" || { log "FAIL CRITICAL $label restore: $final"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  local health_after count_after fail_after
  health_after="$(fetch_async_health 5 || true)"; count_after="$(json_uint_field "$health_after" "$request_counter")"; fail_after="$(json_uint_field "$health_after" "$failure_counter")"
  if [[ "${count_after:-0}" -eq $((count_before+2)) && "${fail_after:-0}" -eq "$fail_before" ]]; then log "PASS $label counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"; else log "FAIL $label counters"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); fi
}

test_persistent_number_setting() {
  local label="$1" path="$2" json_field="$3" request_counter="$4" failure_counter="$5"
  local before original runtime test_value health_before count_before fail_before meta rc http seconds bytes response verify final
  before="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"; original="$(json_number_field "$before" stored)"; runtime="$(json_number_field "$before" runtime)"
  if [[ -z "$original" ]]; then log "FAIL $label pre-read: $before"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); return; fi
  test_value="$(awk -v v="$original" 'BEGIN{ if (v < 0.90) printf "0.910"; else printf "0.310" }')"
  health_before="$(fetch_async_health 5 || true)"; count_before="$(json_uint_field "$health_before" "$request_counter")"; fail_before="$(json_uint_field "$health_before" "$failure_counter")"; count_before="${count_before:-0}"; fail_before="${fail_before:-0}"
  log "$label original stored=$original runtime=$runtime; temporary test value=$test_value"
  meta="$(post_json_capture "prefs-write" "async-${label}-write" "$BASE8080$path" "{\"$json_field\":$test_value}" "${OUTDIR}/${label}-write.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/${label}-write.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "200" ]] && log "PASS $label persistent write: $response" || { log "FAIL $label persistent write: HTTP=${http:-000} rc=$rc response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  verify="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"; [[ "$(json_number_field "$verify" stored)" == "$test_value" ]] && log "PASS $label NVS readback after write: stored=$test_value" || { log "FAIL $label NVS readback: $verify"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "negative-test" "async-${label}-invalid" "$BASE8080$path" "{\"$json_field\":-1}" "${OUTDIR}/${label}-invalid.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/${label}-invalid.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *'"invalid_power_price"'* ]] && log "PASS $label invalid value rejected" || { log "FAIL $label invalid value handling: HTTP=${http:-000} response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "prefs-restore" "async-${label}-restore" "$BASE8080$path" "{\"$json_field\":$original}" "${OUTDIR}/${label}-restore.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"; final="$(curl -sS --max-time 5 "$BASE8080$path" 2>>"$LOGFILE" || true)"
  [[ "$rc" -eq 0 && "$http" == "200" && "$(json_number_field "$final" stored)" == "$original" ]] && log "PASS $label original persistent value restored: stored=$original" || { log "FAIL CRITICAL $label restore: $final"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  local health_after count_after fail_after
  health_after="$(fetch_async_health 5 || true)"; count_after="$(json_uint_field "$health_after" "$request_counter")"; fail_after="$(json_uint_field "$health_after" "$failure_counter")"
  if [[ "${count_after:-0}" -eq $((count_before+2)) && "${fail_after:-0}" -eq "$fail_before" ]]; then log "PASS $label counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"; else log "FAIL $label counters"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); fi
}

test_persistent_relay_count_setting() {
  local before original runtime test_value health_before count_before fail_before meta rc http seconds bytes response verify final health_after count_after fail_after
  before="$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)"
  original="$(json_uint_field "$before" stored)"; runtime="$(json_uint_field "$before" runtime)"
  if [[ "$original" != "4" && "$original" != "8" ]]; then log "FAIL active-relay-count pre-read: $before"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); return; fi
  [[ "$original" == "4" ]] && test_value=8 || test_value=4
  health_before="$(fetch_async_health 5 || true)"
  count_before="$(json_uint_field "$health_before" relayCountWriteRequests)"; fail_before="$(json_uint_field "$health_before" relayCountWriteFailures)"
  count_before="${count_before:-0}"; fail_before="${fail_before:-0}"
  log "active-relay-count original stored=$original runtime=$runtime; temporary test value=$test_value"
  meta="$(post_json_capture "prefs-write" "async-active-relay-count-write" "$BASE8080/api/settings/active-relay-count" "{\"activeRelayCount\":$test_value}" "${OUTDIR}/active-relay-count-write.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/active-relay-count-write.json" 2>/dev/null || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$response" == *"\"stored\":$test_value"* && "$response" == *"\"runtime\":$runtime"* ]]; then
    log "PASS active-relay-count persistent write with runtime unchanged: $response"
  else
    log "FAIL active-relay-count write/runtime invariant: HTTP=${http:-000} rc=$rc response='$response'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi
  verify="$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)"
  [[ "$(json_uint_field "$verify" stored)" == "$test_value" && "$(json_uint_field "$verify" runtime)" == "$runtime" ]] && log "PASS active-relay-count NVS readback with runtime unchanged" || { log "FAIL active-relay-count NVS/runtime readback: $verify"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "negative-test" "async-active-relay-count-invalid" "$BASE8080/api/settings/active-relay-count" '{"activeRelayCount":6}' "${OUTDIR}/active-relay-count-invalid.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/active-relay-count-invalid.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *'"invalid_active_relay_count"'* ]] && log "PASS active-relay-count invalid value rejected" || { log "FAIL active-relay-count invalid handling: HTTP=${http:-000} response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  meta="$(post_json_capture "prefs-restore" "async-active-relay-count-restore" "$BASE8080/api/settings/active-relay-count" "{\"activeRelayCount\":$original}" "${OUTDIR}/active-relay-count-restore.json" 5)"; rc=$?; IFS=$'\t' read -r http seconds bytes <<< "$meta"
  final="$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)"
  [[ "$rc" -eq 0 && "$http" == "200" && "$(json_uint_field "$final" stored)" == "$original" && "$(json_uint_field "$final" runtime)" == "$runtime" ]] && log "PASS active-relay-count original persistent value restored" || { log "FAIL CRITICAL active-relay-count restore: $final"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
  health_after="$(fetch_async_health 5 || true)"; count_after="$(json_uint_field "$health_after" relayCountWriteRequests)"; fail_after="$(json_uint_field "$health_after" relayCountWriteFailures)"
  count_after="${count_after:-0}"; fail_after="${fail_after:-0}"
  [[ "$count_after" -eq $((count_before+2)) && "$fail_after" -eq "$fail_before" ]] && log "PASS active-relay-count counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after" || { log "FAIL active-relay-count counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
}

test_persistent_shelly_device_setting() {
  local idx="$1" role="$2" original_ip="$3" original_gen="$4" runtime_ip="$5" runtime_gen="$6"
  local temp_ip temp_gen health_before count_before fail_before meta rc http seconds bytes response verify
  local final health_after count_after fail_after escaped_runtime_ip escaped_original_ip
  temp_ip="192.0.2.$((40 + idx))"
  [[ "$original_ip" == "$temp_ip" ]] && temp_ip="198.51.100.$((40 + idx))"
  [[ "$original_gen" == "3" ]] && temp_gen=2 || temp_gen=3
  escaped_runtime_ip="$(json_escape_string "$runtime_ip")"
  escaped_original_ip="$(json_escape_string "$original_ip")"

  health_before="$(fetch_async_health 5 || true)"
  count_before="$(json_uint_field "$health_before" shellyDeviceWriteRequests)"; fail_before="$(json_uint_field "$health_before" shellyDeviceWriteFailures)"
  count_before="${count_before:-0}"; fail_before="${fail_before:-0}"
  log "shelly-$role original storedIp='$original_ip' storedGen=$original_gen runtimeIp='$runtime_ip' runtimeGen=$runtime_gen; temporary=$temp_ip/gen$temp_gen"

  meta="$(post_json_capture "shelly-write" "async-shelly-${role}-write" "$BASE8080/api/settings/shelly/$role" "{\"ip\":\"$temp_ip\",\"gen\":$temp_gen}" "${OUTDIR}/shelly-${role}-write.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/shelly-${role}-write.json" 2>/dev/null || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$response" == *"\"storedIp\":\"$temp_ip\""* && "$response" == *"\"storedGen\":$temp_gen"* && "$response" == *"\"runtimeIp\":\"$escaped_runtime_ip\""* && "$response" == *"\"runtimeGen\":$runtime_gen"* ]]; then
    log "PASS shelly-$role persistent write with runtime unchanged"
  else
    log "FAIL shelly-$role persistent write/runtime invariant: HTTP=${http:-000} rc=$rc response='$response'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi

  verify="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/$role" 2>>"$LOGFILE" || true)"
  if [[ "$(json_string_field "$verify" storedIp)" == "$temp_ip" && "$(json_uint_field "$verify" storedGen)" == "$temp_gen" && "$(json_string_field "$verify" runtimeIp)" == "$runtime_ip" && "$(json_uint_field "$verify" runtimeGen)" == "$runtime_gen" ]]; then
    log "PASS shelly-$role NVS readback with runtime unchanged"
  else
    log "FAIL shelly-$role NVS/runtime readback: $verify"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi

  meta="$(post_json_capture "negative-test" "async-shelly-${role}-invalid-ip" "$BASE8080/api/settings/shelly/$role" '{"ip":"999.999.999.999","gen":2}' "${OUTDIR}/shelly-${role}-invalid-ip.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/shelly-${role}-invalid-ip.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *'"invalid_shelly_ip"'* ]] && log "PASS shelly-$role invalid IP rejected" || { log "FAIL shelly-$role invalid IP handling: HTTP=${http:-000} response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }

  meta="$(post_json_capture "negative-test" "async-shelly-${role}-invalid-gen" "$BASE8080/api/settings/shelly/$role" "{\"ip\":\"$temp_ip\",\"gen\":99}" "${OUTDIR}/shelly-${role}-invalid-gen.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/shelly-${role}-invalid-gen.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *'"invalid_shelly_gen"'* ]] && log "PASS shelly-$role invalid generation rejected" || { log "FAIL shelly-$role invalid generation handling: HTTP=${http:-000} response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }

  meta="$(post_json_capture "shelly-restore" "async-shelly-${role}-restore" "$BASE8080/api/settings/shelly/$role" "{\"ip\":\"$escaped_original_ip\",\"gen\":$original_gen}" "${OUTDIR}/shelly-${role}-restore.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"
  final="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/$role" 2>>"$LOGFILE" || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$(json_string_field "$final" storedIp)" == "$original_ip" && "$(json_uint_field "$final" storedGen)" == "$original_gen" && "$(json_string_field "$final" runtimeIp)" == "$runtime_ip" && "$(json_uint_field "$final" runtimeGen)" == "$runtime_gen" ]]; then
    log "PASS shelly-$role original persistent configuration restored"
  else
    log "FAIL CRITICAL shelly-$role restore: $final"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"; count_after="$(json_uint_field "$health_after" shellyDeviceWriteRequests)"; fail_after="$(json_uint_field "$health_after" shellyDeviceWriteFailures)"
  count_after="${count_after:-0}"; fail_after="${fail_after:-0}"
  if [[ "$count_after" -eq $((count_before+2)) && "$fail_after" -eq "$fail_before" ]]; then
    log "PASS shelly-$role counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"
  else
    log "FAIL shelly-$role counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi
}

test_persistent_shelly_username() {
  local original="$1" runtime="$2" pw_stored="$3" pw_runtime="$4" pw_restart="$5"
  local test_value="AsyncPhase15" escaped_original escaped_runtime health_before count_before fail_before
  local meta rc http seconds bytes response verify final health_after count_after fail_after
  [[ "$original" == "$test_value" ]] && test_value="AsyncPhase15B"
  escaped_original="$(json_escape_string "$original")"; escaped_runtime="$(json_escape_string "$runtime")"
  health_before="$(fetch_async_health 5 || true)"; count_before="$(json_uint_field "$health_before" shellyAuthWriteRequests)"; fail_before="$(json_uint_field "$health_before" shellyAuthWriteFailures)"; count_before="${count_before:-0}"; fail_before="${fail_before:-0}"
  log "shelly-auth username original stored='$original' runtime='$runtime'; password remains opaque and untouched"

  meta="$(post_json_capture "shelly-write" "async-shelly-auth-write" "$BASE8080/api/settings/shelly/auth" "{\"username\":\"$test_value\"}" "${OUTDIR}/shelly-auth-write.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/shelly-auth-write.json" 2>/dev/null || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$response" == *"\"storedUsername\":\"$test_value\""* && "$response" == *"\"runtimeUsername\":\"$escaped_runtime\""* ]]; then
    log "PASS shelly-auth username persistent write with runtime unchanged"
  else
    log "FAIL shelly-auth username write/runtime invariant: HTTP=${http:-000} response='$response'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi
  verify="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/auth" 2>>"$LOGFILE" || true)"
  if [[ "$(json_string_field "$verify" storedUsername)" == "$test_value" && "$(json_string_field "$verify" runtimeUsername)" == "$runtime" && "$(json_bool_field "$verify" storedPasswordSet)" == "$pw_stored" && "$(json_bool_field "$verify" runtimePasswordSet)" == "$pw_runtime" && "$(json_bool_field "$verify" passwordRestartRequired)" == "$pw_restart" ]]; then
    log "PASS shelly-auth password state unchanged and password value never exposed"
  else
    log "FAIL shelly-auth readback/password invariants: $verify"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi

  meta="$(post_json_capture "negative-test" "async-shelly-auth-invalid" "$BASE8080/api/settings/shelly/auth" "{\"username\":\"$(printf 'U%.0s' {1..129})\"}" "${OUTDIR}/shelly-auth-invalid.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; response="$(cat "${OUTDIR}/shelly-auth-invalid.json" 2>/dev/null || true)"
  [[ "$rc" -eq 0 && "$http" == "400" && "$response" == *'"invalid_shelly_username"'* ]] && log "PASS shelly-auth overlong username rejected" || { log "FAIL shelly-auth invalid username handling: HTTP=${http:-000} response='$response'"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }

  meta="$(post_json_capture "shelly-restore" "async-shelly-auth-restore" "$BASE8080/api/settings/shelly/auth" "{\"username\":\"$escaped_original\"}" "${OUTDIR}/shelly-auth-restore.json" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"; final="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/auth" 2>>"$LOGFILE" || true)"
  if [[ "$rc" -eq 0 && "$http" == "200" && "$(json_string_field "$final" storedUsername)" == "$original" && "$(json_string_field "$final" runtimeUsername)" == "$runtime" && "$(json_bool_field "$final" storedPasswordSet)" == "$pw_stored" && "$(json_bool_field "$final" passwordRestartRequired)" == "$pw_restart" ]]; then
    log "PASS shelly-auth original username restored; password state preserved"
  else
    log "FAIL CRITICAL shelly-auth restore/password preservation: $final"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"; count_after="$(json_uint_field "$health_after" shellyAuthWriteRequests)"; fail_after="$(json_uint_field "$health_after" shellyAuthWriteFailures)"; count_after="${count_after:-0}"; fail_after="${fail_after:-0}"
  [[ "$count_after" -eq $((count_before+2)) && "$fail_after" -eq "$fail_before" ]] && log "PASS shelly-auth counters: requests $count_before -> $count_after; failures $fail_before -> $fail_after" || { log "FAIL shelly-auth counters"; PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1)); }
}

# Phase 17 GET helper: use HTTP/1.1 explicitly and retry a few times because the
# external backend may be polling port 8080 concurrently. A transient transport
# miss must not be reinterpreted as an unsafe/unknown heating configuration.
fetch_grow_targets_state() {
  local attempts="${1:-6}" i tmp http rc body
  tmp="${OUTDIR}/grow-targets-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/settings/grow-targets" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN grow-target GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

# Existing reversible UI settings.
theme_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/theme" 2>>"$LOGFILE" || true)"; theme_original="$(json_string_field "$theme_json" stored)"; case "$theme_original" in light) theme_test=dark ;; dark) theme_test=light ;; *) theme_test="" ;; esac
language_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/language" 2>>"$LOGFILE" || true)"; language_original="$(json_string_field "$language_json" stored)"; case "$language_original" in de) language_test=en ;; en) language_test=de ;; *) language_test="" ;; esac
unit_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/unit" 2>>"$LOGFILE" || true)"; unit_original="$(json_string_field "$unit_json" stored)"; case "$unit_original" in metric) unit_test=imperial ;; imperial) unit_test=metric ;; C) unit_test=F ;; F) unit_test=C ;; *) unit_test="" ;; esac
time_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/time-format" 2>>"$LOGFILE" || true)"; time_original="$(json_string_field "$time_json" stored)"; case "$time_original" in 24h) time_test=12h ;; 12h) time_test=24h ;; 24) time_test=12 ;; 12) time_test=24 ;; *) time_test="" ;; esac
box_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/box-name" 2>>"$LOGFILE" || true)"; box_original="$(json_string_field "$box_json" stored)"; box_test="AsyncTest-v0265"
ds_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/ds18b20-name" 2>>"$LOGFILE" || true)"; ds_original="$(json_string_field "$ds_json" stored)"; ds_test="AsyncWaterProbe"
relay1_original="$(json_string_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/relay-name/1" 2>>"$LOGFILE" || true)" stored)"
relay2_original="$(json_string_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/relay-name/2" 2>>"$LOGFILE" || true)" stored)"
relay3_original="$(json_string_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/relay-name/3" 2>>"$LOGFILE" || true)" stored)"
relay4_original="$(json_string_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/relay-name/4" 2>>"$LOGFILE" || true)" stored)"
relay5_original="$(json_string_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/relay-name/5" 2>>"$LOGFILE" || true)" stored)"
debug_original="$(json_bool_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/debug" 2>>"$LOGFILE" || true)" stored)"
power_original="$(json_number_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/power-price" 2>>"$LOGFILE" || true)" stored)"
ntp_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/ntp-server" 2>>"$LOGFILE" || true)"; ntp_original="$(json_string_field "$ntp_json" stored)"
tz_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/time-zone" 2>>"$LOGFILE" || true)"; tz_original="$(json_string_field "$tz_json" stored)"
ds18_enable_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/ds18b20-enabled" 2>>"$LOGFILE" || true)"; ds18_enable_original="$(json_bool_field "$ds18_enable_json" stored)"
relay_count_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)"; relay_count_original="$(json_uint_field "$relay_count_json" stored)"
grow_targets_json="$(fetch_grow_targets_state 6 || true)"
grow_temp_stored_original="$(json_nested_number_field "$grow_targets_json" stored targetTemperature)"
grow_vpd_stored_original="$(json_nested_number_field "$grow_targets_json" stored targetVPD)"
grow_leaf_stored_original="$(json_nested_number_field "$grow_targets_json" stored offsetLeafTemperature)"
grow_temp_runtime_original="$(json_nested_number_field "$grow_targets_json" runtime targetTemperature)"
grow_vpd_runtime_original="$(json_nested_number_field "$grow_targets_json" runtime targetVPD)"
grow_leaf_runtime_original="$(json_nested_number_field "$grow_targets_json" runtime offsetLeafTemperature)"
grow_control_outputs_configured="$(json_bool_field "$grow_targets_json" controlOutputsConfigured)"
grow_heating_source="$(json_uint_field "$grow_targets_json" heatingSource)"
SHELLY_ROLES=(main light humidifier heater fan exhaust heatmat)
declare -a SHELLY_ORIGINAL_IP SHELLY_ORIGINAL_GEN SHELLY_RUNTIME_IP SHELLY_RUNTIME_GEN
for idx in "${!SHELLY_ROLES[@]}"; do
  role="${SHELLY_ROLES[$idx]}"
  sj="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/$role" 2>>"$LOGFILE" || true)"
  SHELLY_ORIGINAL_IP[$idx]="$(json_string_field "$sj" storedIp)"
  SHELLY_ORIGINAL_GEN[$idx]="$(json_uint_field "$sj" storedGen)"
  SHELLY_RUNTIME_IP[$idx]="$(json_string_field "$sj" runtimeIp)"
  SHELLY_RUNTIME_GEN[$idx]="$(json_uint_field "$sj" runtimeGen)"
done
shelly_auth_json="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/auth" 2>>"$LOGFILE" || true)"
shelly_user_original="$(json_string_field "$shelly_auth_json" storedUsername)"
shelly_user_runtime="$(json_string_field "$shelly_auth_json" runtimeUsername)"
shelly_pw_stored="$(json_bool_field "$shelly_auth_json" storedPasswordSet)"
shelly_pw_runtime="$(json_bool_field "$shelly_auth_json" runtimePasswordSet)"
shelly_pw_restart="$(json_bool_field "$shelly_auth_json" passwordRestartRequired)"
if [[ "$ntp_original" == "pool.ntp.org" ]]; then ntp_test="time.cloudflare.com"; else ntp_test="pool.ntp.org"; fi
if [[ "$tz_original" == "UTC0" ]]; then tz_test="CET-1CEST,M3.5.0,M10.5.0"; else tz_test="UTC0"; fi

cat > "${OUTDIR}/system-settings-original.txt" <<EOF
theme=$theme_original
language=$language_original
unit=$unit_original
timeFormat=$time_original
boxName=$box_original
ds18b20Name=$ds_original
debug=$debug_original
powerPrice=$power_original
ntpServer=$ntp_original
timeZoneInfo=$tz_original
ds18b20Enabled=$ds18_enable_original
activeRelayCount=$relay_count_original
grow.targetTemperature=$grow_temp_stored_original
grow.targetVPD=$grow_vpd_stored_original
grow.offsetLeafTemperature=$grow_leaf_stored_original
relay1=$relay1_original
relay2=$relay2_original
relay3=$relay3_original
relay4=$relay4_original
relay5=$relay5_original
EOF
for idx in "${!SHELLY_ROLES[@]}"; do
  printf 'shelly.%s.ip=%s\nshelly.%s.gen=%s\n' "${SHELLY_ROLES[$idx]}" "${SHELLY_ORIGINAL_IP[$idx]}" "${SHELLY_ROLES[$idx]}" "${SHELLY_ORIGINAL_GEN[$idx]}" >> "${OUTDIR}/system-settings-original.txt"
done
printf 'shelly.username=%s\n' "$shelly_user_original" >> "${OUTDIR}/system-settings-original.txt"
printf 'shelly.password=<not exported; state only>\n' >> "${OUTDIR}/system-settings-original.txt"
log "Original safe-system/Shelly/grow-target settings snapshot saved in: ${OUTDIR}/system-settings-original.txt"

PHASE14_CLEANUP_ARMED=0
phase14_cleanup() {
  [[ "${PHASE14_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE14_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring DS18B20 enable and active relay count after interrupted/incomplete Phase 14 test"
  curl -sS --max-time 5 -H 'Content-Type: application/json' -X POST --data-binary "{\"ds18b20Enabled\":$ds18_enable_original}" "$BASE8080/api/settings/ds18b20-enabled" -o /dev/null 2>>"$LOGFILE" || true
  curl -sS --max-time 5 -H 'Content-Type: application/json' -X POST --data-binary "{\"activeRelayCount\":$relay_count_original}" "$BASE8080/api/settings/active-relay-count" -o /dev/null 2>>"$LOGFILE" || true
}
phase14_signal_cleanup() {
  phase14_cleanup
  exit 130
}
trap phase14_cleanup EXIT
trap phase14_signal_cleanup INT TERM

if [[ -z "$theme_test" || -z "$language_test" || -z "$unit_test" || -z "$time_test" || -z "$box_original" || ${#box_original} -gt 64 ]]; then
  log "FAIL unsupported pre-existing UI setting representation"
  PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
else
  test_persistent_string_setting "theme" "/api/settings/theme" "theme" "$theme_test" "purple" "invalid_theme" "themeWriteRequests" "themeWriteFailures"
  test_persistent_string_setting "language" "/api/settings/language" "language" "$language_test" "fr" "invalid_language" "languageWriteRequests" "languageWriteFailures"
  test_persistent_string_setting "unit" "/api/settings/unit" "unit" "$unit_test" "kelvin" "invalid_unit" "unitWriteRequests" "unitWriteFailures"
  test_persistent_string_setting "time-format" "/api/settings/time-format" "timeFormat" "$time_test" "25h" "invalid_time_format" "timeFormatWriteRequests" "timeFormatWriteFailures"
  test_persistent_string_setting "box-name" "/api/settings/box-name" "boxName" "$box_test" "ThisBoxNameIsDeliberatelyLongerThanSixtyFourCharacters-And-Must-Be-Rejected-By-The-Async-Endpoint" "invalid_box_name" "boxNameWriteRequests" "boxNameWriteFailures"
  test_persistent_string_setting "ds18b20-name" "/api/settings/ds18b20-name" "ds18b20Name" "$ds_test" "$(printf 'X%.0s' {1..401})" "invalid_ds18b20_name" "ds18NameWriteRequests" "ds18NameWriteFailures"
  test_persistent_string_setting "relay-name-1" "/api/settings/relay-name/1" "relayName" "Async-R1" "1234567890123456" "invalid_relay_name" "relayNameWriteRequests" "relayNameWriteFailures"
  test_persistent_string_setting "relay-name-2" "/api/settings/relay-name/2" "relayName" "Async-R2" "1234567890123456" "invalid_relay_name" "relayNameWriteRequests" "relayNameWriteFailures"
  test_persistent_string_setting "relay-name-3" "/api/settings/relay-name/3" "relayName" "Async-R3" "1234567890123456" "invalid_relay_name" "relayNameWriteRequests" "relayNameWriteFailures"
  test_persistent_string_setting "relay-name-4" "/api/settings/relay-name/4" "relayName" "Async-R4" "1234567890123456" "invalid_relay_name" "relayNameWriteRequests" "relayNameWriteFailures"
  test_persistent_string_setting "relay-name-5" "/api/settings/relay-name/5" "relayName" "Async-R5" "1234567890123456" "invalid_relay_name" "relayNameWriteRequests" "relayNameWriteFailures"
  test_persistent_bool_setting "debug" "/api/settings/debug" "debug" "debugWriteRequests" "debugWriteFailures"
  test_persistent_number_setting "power-price" "/api/settings/power-price" "powerPrice" "powerPriceWriteRequests" "powerPriceWriteFailures"

  phase13_failures_before="$PREFWRITE_FAILURES"
  test_persistent_string_setting "ntp-server" "/api/settings/ntp-server" "ntpServer" "$ntp_test" "$(printf 'n%.0s' {1..254})" "invalid_ntp_server" "ntpServerWriteRequests" "ntpServerWriteFailures"
  test_persistent_string_setting "time-zone" "/api/settings/time-zone" "timeZoneInfo" "$tz_test" "$(printf 'T%.0s' {1..256})" "invalid_time_zone" "timeZoneWriteRequests" "timeZoneWriteFailures"
  if [[ "$PREFWRITE_FAILURES" -eq "$phase13_failures_before" ]]; then
    log "ASYNC NTP/TIME-ZONE WRITE/RESTORE CHECK: PASS"
  else
    log "ASYNC NTP/TIME-ZONE WRITE/RESTORE CHECK: FAIL"
  fi
phase14_failures_before="$PREFWRITE_FAILURES"
  if [[ "$ds18_enable_original" != "true" && "$ds18_enable_original" != "false" ]]; then
    log "FAIL unsupported DS18B20 enable pre-existing representation: '$ds18_enable_original'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  elif [[ "$relay_count_original" != "4" && "$relay_count_original" != "8" ]]; then
    log "FAIL unsupported active relay count pre-existing representation: '$relay_count_original'"
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
  else
    PHASE14_CLEANUP_ARMED=1
    test_persistent_bool_setting "ds18b20-enabled" "/api/settings/ds18b20-enabled" "ds18b20Enabled" "ds18EnableWriteRequests" "ds18EnableWriteFailures" "invalid_ds18b20_enabled"
    test_persistent_relay_count_setting
    # Both helpers restored their original NVS values. Verify once more before
    # disarming the EXIT/CTRL-C cleanup because these settings affect hardware
    # behaviour only after a reboot.
    ds18_final="$(json_bool_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/ds18b20-enabled" 2>>"$LOGFILE" || true)" stored)"
    relay_count_final="$(json_uint_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)" stored)"
    if [[ "$ds18_final" == "$ds18_enable_original" && "$relay_count_final" == "$relay_count_original" ]]; then
      PHASE14_CLEANUP_ARMED=0
      log "PASS Phase 14 safety restore verification; automatic cleanup disarmed"
    else
      log "FAIL CRITICAL Phase 14 final restore verification: ds18=$ds18_final/$ds18_enable_original relayCount=$relay_count_final/$relay_count_original"
      PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
      phase14_cleanup
      ds18_final="$(json_bool_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/ds18b20-enabled" 2>>"$LOGFILE" || true)" stored)"
      relay_count_final="$(json_uint_field "$(curl -sS --max-time 5 "$BASE8080/api/settings/active-relay-count" 2>>"$LOGFILE" || true)" stored)"
      if [[ "$ds18_final" != "$ds18_enable_original" || "$relay_count_final" != "$relay_count_original" ]]; then
        log "ABORT CRITICAL: Phase 14 safety cleanup could not confirm original NVS values; do not reboot until values are checked manually."
        exit 4
      fi
      log "PASS Phase 14 emergency cleanup restored original NVS values"
    fi
  fi
  if [[ "$PREFWRITE_FAILURES" -eq "$phase14_failures_before" ]]; then
    log "ASYNC RESTART-APPLIED SYSTEM SETTINGS WRITE/RESTORE CHECK: PASS"
  else
    log "ASYNC RESTART-APPLIED SYSTEM SETTINGS WRITE/RESTORE CHECK: FAIL"
  fi
fi

# v0.3.08 retains changed-only System Settings logging contract. The Phase-0F writes above
# deliberately changed and restored several settings, so at least one changed log
# must have been emitted. A same-value write must remain silent.
settings_log_health_before_noop="$(fetch_async_health 5 || true)"
settings_log_count_before_noop="$(json_uint_field "$settings_log_health_before_noop" systemSettingsChangedLogs)"
settings_log_count_before_noop="${settings_log_count_before_noop:-0}"
if [[ "$settings_log_count_before_noop" -gt 0 ]]; then
  log "PASS System Settings changed-value logging counter is active: $settings_log_count_before_noop"
else
  log "FAIL System Settings changed-value logging counter did not increment during Phase 0F"
  PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
fi
settings_theme_same_escaped="$(json_escape_string "$theme_original")"
settings_same_body="${OUTDIR}/system-settings-same-value-log-check.json"
settings_same_meta="$(post_json_capture "prefs-noop" "async-system-settings-same-value" \
  "$BASE8080/api/settings/theme" "{\"theme\":\"$settings_theme_same_escaped\"}" "$settings_same_body" 5)"
settings_same_rc=$?
IFS=$'\t' read -r settings_same_http settings_same_seconds settings_same_bytes <<< "$settings_same_meta"
settings_log_health_after_noop="$(fetch_async_health 5 || true)"
settings_log_count_after_noop="$(json_uint_field "$settings_log_health_after_noop" systemSettingsChangedLogs)"
settings_log_count_after_noop="${settings_log_count_after_noop:-0}"
if [[ "$settings_same_rc" -eq 0 && "$settings_same_http" == "200" && "$settings_log_count_after_noop" -eq "$settings_log_count_before_noop" ]]; then
  log "PASS System Settings same-value save stays silent: changedLogs $settings_log_count_before_noop -> $settings_log_count_after_noop"
  log "SYSTEM SETTINGS CHANGE-ONLY LOGGING CHECK: PASS"
else
  log "FAIL System Settings same-value logging contract: HTTP=${settings_same_http:-000} rc=$settings_same_rc changedLogs $settings_log_count_before_noop -> $settings_log_count_after_noop"
  log "SYSTEM SETTINGS CHANGE-ONLY LOGGING CHECK: FAIL"
  PREFWRITE_FAILURES=$((PREFWRITE_FAILURES + 1))
fi

log ""
log "===== PHASE 0G: PERSISTENT ASYNC SHELLY CONFIGURATION (NVS-ONLY) ====="
# Phase 15 is intentionally NVS-only. Arm a second cleanup while the seven
# device tuples and username are temporarily changed. The password is never read
# or written by this test.
PHASE15_CLEANUP_ARMED=0
phase15_cleanup() {
  [[ "${PHASE15_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE15_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring Shelly device configuration and username after interrupted/incomplete Phase 15 test"
  for idx in "${!SHELLY_ROLES[@]}"; do
    role="${SHELLY_ROLES[$idx]}"; ip="$(json_escape_string "${SHELLY_ORIGINAL_IP[$idx]}")"; gen="${SHELLY_ORIGINAL_GEN[$idx]}"
    curl -sS --max-time 5 -H 'Content-Type: application/json' -X POST --data-binary "{\"ip\":\"$ip\",\"gen\":$gen}" "$BASE8080/api/settings/shelly/$role" -o /dev/null 2>>"$LOGFILE" || true
  done
  user_escaped="$(json_escape_string "$shelly_user_original")"
  curl -sS --max-time 5 -H 'Content-Type: application/json' -X POST --data-binary "{\"username\":\"$user_escaped\"}" "$BASE8080/api/settings/shelly/auth" -o /dev/null 2>>"$LOGFILE" || true
}
phase15_signal_cleanup() { phase15_cleanup; exit 130; }
trap phase15_cleanup EXIT
trap phase15_signal_cleanup INT TERM

phase15_failures_before="$PREFWRITE_FAILURES"
phase15_precheck_ok=1
for idx in "${!SHELLY_ROLES[@]}"; do
  if [[ -z "${SHELLY_ORIGINAL_GEN[$idx]}" || -z "${SHELLY_RUNTIME_GEN[$idx]}" ]]; then
    log "FAIL Shelly ${SHELLY_ROLES[$idx]} pre-read incomplete; refusing temporary write"
    phase15_precheck_ok=0
  fi
done
if [[ "$shelly_pw_stored" != "true" && "$shelly_pw_stored" != "false" ]]; then phase15_precheck_ok=0; log "FAIL Shelly auth password-state pre-read incomplete"; fi
if [[ "$shelly_pw_runtime" != "true" && "$shelly_pw_runtime" != "false" ]]; then phase15_precheck_ok=0; log "FAIL Shelly auth runtime password-state pre-read incomplete"; fi
if [[ "$shelly_pw_restart" != "true" && "$shelly_pw_restart" != "false" ]]; then phase15_precheck_ok=0; log "FAIL Shelly auth password restart-state pre-read incomplete"; fi

if [[ "$phase15_precheck_ok" -eq 1 ]]; then
  PHASE15_CLEANUP_ARMED=1
  for idx in "${!SHELLY_ROLES[@]}"; do
    test_persistent_shelly_device_setting "$idx" "${SHELLY_ROLES[$idx]}" "${SHELLY_ORIGINAL_IP[$idx]}" "${SHELLY_ORIGINAL_GEN[$idx]}" "${SHELLY_RUNTIME_IP[$idx]}" "${SHELLY_RUNTIME_GEN[$idx]}"
  done
  test_persistent_shelly_username "$shelly_user_original" "$shelly_user_runtime" "$shelly_pw_stored" "$shelly_pw_runtime" "$shelly_pw_restart"

  phase15_restore_ok=1
  for idx in "${!SHELLY_ROLES[@]}"; do
    role="${SHELLY_ROLES[$idx]}"; sj="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/$role" 2>>"$LOGFILE" || true)"
    if [[ "$(json_string_field "$sj" storedIp)" != "${SHELLY_ORIGINAL_IP[$idx]}" || "$(json_uint_field "$sj" storedGen)" != "${SHELLY_ORIGINAL_GEN[$idx]}" ]]; then
      phase15_restore_ok=0
      log "FAIL CRITICAL Phase 15 final restore verification for role=$role: $sj"
    fi
  done
  auth_final="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly/auth" 2>>"$LOGFILE" || true)"
  if [[ "$(json_string_field "$auth_final" storedUsername)" != "$shelly_user_original" || "$(json_bool_field "$auth_final" storedPasswordSet)" != "$shelly_pw_stored" || "$(json_bool_field "$auth_final" passwordRestartRequired)" != "$shelly_pw_restart" ]]; then
    phase15_restore_ok=0
    log "FAIL CRITICAL Phase 15 final auth restore/password-state verification: $auth_final"
  fi
  if [[ "$phase15_restore_ok" -eq 1 ]]; then
    PHASE15_CLEANUP_ARMED=0
    log "PASS Phase 15 Shelly safety restore verification; automatic cleanup disarmed"
  else
    PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
    phase15_cleanup
    log "ABORT CRITICAL: Phase 15 required emergency cleanup. Verify Shelly NVS values before rebooting."
    exit 5
  fi
else
  PREFWRITE_FAILURES=$((PREFWRITE_FAILURES+1))
fi

if [[ "$PREFWRITE_FAILURES" -eq "$phase15_failures_before" ]]; then
  log "ASYNC SHELLY PERSISTENT CONFIG WRITE/RESTORE CHECK: PASS"
else
  log "ASYNC SHELLY PERSISTENT CONFIG WRITE/RESTORE CHECK: FAIL"
fi

if [[ "$PREFWRITE_FAILURES" -eq 0 ]]; then
  log "ASYNC SAFE SYSTEM + RESTART-APPLIED + SHELLY SETTINGS WRITE/RESTORE CHECK: PASS"
else
  log "ASYNC SAFE SYSTEM + RESTART-APPLIED + SHELLY SETTINGS WRITE/RESTORE CHECK: FAIL ($PREFWRITE_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 17: first runtime-applied settings via main-loop handoff.
# Safety rule: the automated temporary mutation is allowed only on a board that
# has no configured heater/humidifier/exhaust output. This prevents a test value
# from indirectly switching real hardware through the existing control tasks.
# ---------------------------------------------------------------------------
RUNTIME_GROW_FAILURES=0
PHASE17_CLEANUP_ARMED=0

wait_for_grow_targets() {
  local want_temp="$1" want_vpd="$2" want_leaf="$3" timeout_steps="${4:-40}"
  local i state rt rv rl pending
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_grow_targets_state 3 || true)"
    rt="$(json_nested_number_field "$state" runtime targetTemperature)"
    rv="$(json_nested_number_field "$state" runtime targetVPD)"
    rl="$(json_nested_number_field "$state" runtime offsetLeafTemperature)"
    pending="$(json_bool_field "$state" pending)"
    if [[ -n "$rt" && -n "$rv" && -n "$rl" ]] && \
       float_close "$rt" "$want_temp" 0.005 && \
       float_close "$rv" "$want_vpd" 0.005 && \
       float_close "$rl" "$want_leaf" 0.005 && \
       [[ "$pending" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

phase17_restore_now() {
  local response_file="${OUTDIR}/grow-targets-emergency-restore.json"
  local attempt state meta rc http seconds bytes
  for attempt in $(seq 1 20); do
    meta="$(post_json_capture "grow-runtime-restore" "async-grow-targets-emergency-restore" \
      "$BASE8080/api/settings/grow-targets" \
      "{\"targetTemperature\":$grow_temp_stored_original,\"targetVPD\":$grow_vpd_stored_original,\"offsetLeafTemperature\":$grow_leaf_stored_original}" \
      "$response_file" 5)"; rc=$?
    IFS=$'\t' read -r http seconds bytes <<< "$meta"
    if [[ "$rc" -eq 0 && "$http" == "202" ]]; then
      if state="$(wait_for_grow_targets "$grow_temp_runtime_original" "$grow_vpd_runtime_original" "$grow_leaf_runtime_original" 60)"; then
        return 0
      fi
    elif [[ "$http" == "409" ]]; then
      sleep 0.05
      continue
    fi
    sleep 0.05
  done
  return 1
}

phase17_cleanup() {
  [[ "${PHASE17_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE17_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring original grow targets in NVS and runtime after interrupted/incomplete Phase 17 test"
  if phase17_restore_now; then
    log "PASS Phase 17 emergency grow-target cleanup restored original runtime/NVS values"
  else
    log "FAIL CRITICAL Phase 17 emergency grow-target cleanup could not confirm original runtime values"
  fi
}
phase17_signal_cleanup() { phase17_cleanup; exit 130; }
trap phase17_cleanup EXIT
trap phase17_signal_cleanup INT TERM

log "===== PHASE 0H: RUNTIME GROW-TARGET HANDOFF (MAIN LOOP) ====="
phase17_precheck=1
phase17_safety_skip=0
for v in "$grow_temp_stored_original" "$grow_vpd_stored_original" "$grow_leaf_stored_original" \
         "$grow_temp_runtime_original" "$grow_vpd_runtime_original" "$grow_leaf_runtime_original"; do
  [[ -n "$v" ]] || phase17_precheck=0
done
if [[ -z "$grow_targets_json" ]]; then
  phase17_precheck=0
  log "FAIL grow-target precheck: GET /api/settings/grow-targets returned no valid HTTP 200 JSON after retries"
elif [[ "$grow_control_outputs_configured" != "false" ]]; then
  phase17_precheck=0
  phase17_safety_skip=1
  log "SAFE SKIP: controlOutputsConfigured=$grow_control_outputs_configured heatingSource=${grow_heating_source:-unknown}; temporary runtime grow-target mutation is intentionally not run on a board with live control outputs."
fi
if [[ "$phase17_precheck" -eq 1 ]] && \
   ! float_close "$grow_temp_stored_original" "$grow_temp_runtime_original" 0.005; then
  phase17_precheck=0; log "FAIL grow-target precheck: stored/runtime targetTemperature already differ"
fi
if [[ "$phase17_precheck" -eq 1 ]] && \
   ! float_close "$grow_vpd_stored_original" "$grow_vpd_runtime_original" 0.005; then
  phase17_precheck=0; log "FAIL grow-target precheck: stored/runtime targetVPD already differ"
fi
if [[ "$phase17_precheck" -eq 1 ]] && \
   ! float_close "$grow_leaf_stored_original" "$grow_leaf_runtime_original" 0.005; then
  phase17_precheck=0; log "FAIL grow-target precheck: stored/runtime leaf offset already differ"
fi

if [[ "$phase17_precheck" -eq 1 ]]; then
  grow_temp_test="$(awk -v x="$grow_temp_runtime_original" 'BEGIN{if(x<=29.7) printf "%.2f",x+0.2; else printf "%.2f",x-0.2}')"
  grow_vpd_test="$(awk -v x="$grow_vpd_runtime_original" 'BEGIN{if(x<=3.44) printf "%.3f",x+0.05; else printf "%.3f",x-0.05}')"
  grow_leaf_test="$(awk -v x="$grow_leaf_runtime_original" 'BEGIN{if(x<=-0.11) printf "%.2f",x+0.1; else printf "%.2f",x-0.1}')"

  health_before="$(fetch_async_health 5 || true)"
  grow_req_before="$(json_uint_field "$health_before" growTargetsWriteRequests)"; grow_req_before="${grow_req_before:-0}"
  grow_fail_before="$(json_uint_field "$health_before" growTargetsWriteFailures)"; grow_fail_before="${grow_fail_before:-0}"
  grow_applied_before="$(json_uint_field "$health_before" growTargetsApplied)"; grow_applied_before="${grow_applied_before:-0}"

  log "grow-target originals stored/runtime: temp=$grow_temp_runtime_original VPD=$grow_vpd_runtime_original leaf=$grow_leaf_runtime_original"
  log "grow-target temporary runtime test: temp=$grow_temp_test VPD=$grow_vpd_test leaf=$grow_leaf_test"
  PHASE17_CLEANUP_ARMED=1

  grow_write_file="${OUTDIR}/grow-targets-write.json"
  grow_meta="$(post_json_capture "grow-runtime-write" "async-grow-targets-write" \
    "$BASE8080/api/settings/grow-targets" \
    "{\"targetTemperature\":$grow_temp_test,\"targetVPD\":$grow_vpd_test,\"offsetLeafTemperature\":$grow_leaf_test}" \
    "$grow_write_file" 5)"; grow_rc=$?
  IFS=$'\t' read -r grow_http grow_seconds grow_bytes <<< "$grow_meta"
  grow_response="$(cat "$grow_write_file" 2>/dev/null || true)"
  grow_request_id="$(json_uint_field "$grow_response" requestId)"
  if [[ "$grow_rc" -eq 0 && "$grow_http" == "202" && -n "$grow_request_id" && "$grow_response" == *'"applyQueued":true'* ]]; then
    log "PASS grow-target POST persisted and queued for main-loop apply: requestId=$grow_request_id"
  else
    log "FAIL grow-target POST/queue: HTTP=${grow_http:-000} rc=$grow_rc response='$grow_response'"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi

  if grow_after="$(wait_for_grow_targets "$grow_temp_test" "$grow_vpd_test" "$grow_leaf_test" 60)"; then
    applied_id="$(json_uint_field "$grow_after" lastAppliedId)"
    if [[ "$applied_id" == "$grow_request_id" ]]; then
      log "PASS grow-target runtime handoff applied in main loop: requestId=$applied_id"
    else
      log "FAIL grow-target applied ID mismatch: queued=$grow_request_id applied=${applied_id:-missing} state='$grow_after'"
      RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
    fi
  else
    log "FAIL grow-target runtime values were not observed after main-loop handoff: $grow_after"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi

  grow_state="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
  state_temp="$(json_number_field "$grow_state" 'settings.grow.targetTemperature')"
  state_vpd="$(json_number_field "$grow_state" 'settings.grow.targetVPD')"
  state_leaf="$(json_number_field "$grow_state" 'settings.grow.offsetLeafTemperature')"
  if [[ -n "$state_temp" && -n "$state_vpd" && -n "$state_leaf" ]] && \
     float_close "$state_temp" "$grow_temp_test" 0.051 && \
     float_close "$state_vpd" "$grow_vpd_test" 0.011 && \
     float_close "$state_leaf" "$grow_leaf_test" 0.051; then
    log "PASS /api/state grow-target visibility: temp=$state_temp vpd=$state_vpd leaf=$state_leaf"
  else
    log "FAIL /api/state grow-target visibility: temp=${state_temp:-missing} vpd=${state_vpd:-missing} leaf=${state_leaf:-missing}"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi

  bad_meta="$(post_json_capture "negative-test" "async-grow-targets-invalid" \
    "$BASE8080/api/settings/grow-targets" \
    '{"targetTemperature":31.0,"targetVPD":1.0,"offsetLeafTemperature":-1.0}' \
    "${OUTDIR}/grow-targets-invalid.json" 5)"; bad_rc=$?
  IFS=$'\t' read -r bad_http bad_seconds bad_bytes <<< "$bad_meta"
  bad_response="$(cat "${OUTDIR}/grow-targets-invalid.json" 2>/dev/null || true)"
  if [[ "$bad_rc" -eq 0 && "$bad_http" == "400" && "$bad_response" == *'"grow_targets_out_of_range"'* ]]; then
    log "PASS invalid grow targets rejected before persistence/runtime apply"
  else
    log "FAIL invalid grow-target handling: HTTP=${bad_http:-000} response='$bad_response'"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi

  if phase17_restore_now; then
    final_grow="$(fetch_grow_targets_state 6 || true)"
    final_st="$(json_nested_number_field "$final_grow" stored targetTemperature)"
    final_sv="$(json_nested_number_field "$final_grow" stored targetVPD)"
    final_sl="$(json_nested_number_field "$final_grow" stored offsetLeafTemperature)"
    final_rt="$(json_nested_number_field "$final_grow" runtime targetTemperature)"
    final_rv="$(json_nested_number_field "$final_grow" runtime targetVPD)"
    final_rl="$(json_nested_number_field "$final_grow" runtime offsetLeafTemperature)"
    if float_close "$final_st" "$grow_temp_stored_original" 0.005 && \
       float_close "$final_sv" "$grow_vpd_stored_original" 0.005 && \
       float_close "$final_sl" "$grow_leaf_stored_original" 0.005 && \
       float_close "$final_rt" "$grow_temp_runtime_original" 0.005 && \
       float_close "$final_rv" "$grow_vpd_runtime_original" 0.005 && \
       float_close "$final_rl" "$grow_leaf_runtime_original" 0.005; then
      PHASE17_CLEANUP_ARMED=0
      log "PASS Phase 17 original grow targets restored in both NVS and runtime; automatic cleanup disarmed"
    else
      log "FAIL CRITICAL grow-target restore verification: $final_grow"
      RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
    fi
  else
    log "FAIL CRITICAL grow-target restore request/handoff failed"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"
  grow_req_after="$(json_uint_field "$health_after" growTargetsWriteRequests)"; grow_req_after="${grow_req_after:-0}"
  grow_fail_after="$(json_uint_field "$health_after" growTargetsWriteFailures)"; grow_fail_after="${grow_fail_after:-0}"
  grow_applied_after="$(json_uint_field "$health_after" growTargetsApplied)"; grow_applied_after="${grow_applied_after:-0}"
  if [[ "$grow_req_after" -eq $((grow_req_before+2)) && "$grow_fail_after" -eq "$grow_fail_before" && "$grow_applied_after" -eq $((grow_applied_before+2)) ]]; then
    log "PASS grow-target counters: writes $grow_req_before->$grow_req_after applied $grow_applied_before->$grow_applied_after failures $grow_fail_before->$grow_fail_after"
  else
    log "FAIL grow-target counters: writes $grow_req_before->$grow_req_after applied $grow_applied_before->$grow_applied_after failures $grow_fail_before->$grow_fail_after"
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
  fi
else
  if [[ "$phase17_safety_skip" -eq 1 ]]; then
    log "RUNTIME GROW-TARGET HANDOFF: SAFE SKIP (live control output configured; no mutation attempted)"
  else
    RUNTIME_GROW_FAILURES=$((RUNTIME_GROW_FAILURES+1))
    log "RUNTIME GROW-TARGET HANDOFF: NOT EXECUTED because the precondition check failed"
  fi
fi

if [[ "$RUNTIME_GROW_FAILURES" -eq 0 ]]; then
  log "RUNTIME GROW-TARGET MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME GROW-TARGET MAIN-LOOP HANDOFF: FAIL ($RUNTIME_GROW_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 18: runtime-applied heater-controller tuning.
# Safety rule: the automated temporary mutation runs only while heating is
# disabled (sourceType == 0). The endpoint itself never switches a relay/Shelly;
# the normal main loop applies only the four tuning values.
# ---------------------------------------------------------------------------
RUNTIME_HEAT_FAILURES=0
PHASE18_CLEANUP_ARMED=0

fetch_heating_tuning_state() {
  local attempts="${1:-6}" i tmp http rc body
  tmp="${OUTDIR}/heating-tuning-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/settings/heating-tuning" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN heating-tuning GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_heating_tuning() {
  local want_hyst="$1" want_predict="$2" want_min_on="$3" want_min_off="$4" timeout_steps="${5:-60}"
  local i state rh rp r_on r_off pending
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_heating_tuning_state 3 || true)"
    rh="$(json_nested_number_field "$state" runtime hysteresisC)"
    rp="$(json_nested_number_field "$state" runtime predictSeconds)"
    r_on="$(json_nested_number_field "$state" runtime minOnSeconds)"
    r_off="$(json_nested_number_field "$state" runtime minOffSeconds)"
    pending="$(json_bool_field "$state" pending)"
    if [[ -n "$rh" && -n "$rp" && -n "$r_on" && -n "$r_off" ]] && \
       float_close "$rh" "$want_hyst" 0.005 && \
       float_close "$rp" "$want_predict" 0.051 && \
       [[ "$r_on" == "$want_min_on" && "$r_off" == "$want_min_off" ]] && \
       [[ "$pending" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

phase18_restore_now() {
  local response_file="${OUTDIR}/heating-tuning-emergency-restore.json"
  local attempt state meta rc http seconds bytes
  for attempt in $(seq 1 20); do
    meta="$(post_json_capture "heat-runtime-restore" "async-heating-tuning-emergency-restore" \
      "$BASE8080/api/settings/heating-tuning" \
      "{\"hysteresisC\":$heat_hyst_stored_original,\"predictSeconds\":$heat_predict_stored_original,\"minOnSeconds\":$heat_min_on_stored_original,\"minOffSeconds\":$heat_min_off_stored_original}" \
      "$response_file" 5)"; rc=$?
    IFS=$'\t' read -r http seconds bytes <<< "$meta"
    if [[ "$rc" -eq 0 && "$http" == "202" ]]; then
      if state="$(wait_for_heating_tuning "$heat_hyst_runtime_original" "$heat_predict_runtime_original" "$heat_min_on_runtime_original" "$heat_min_off_runtime_original" 60)"; then
        return 0
      fi
    elif [[ "$http" == "409" ]]; then
      sleep 0.05
      continue
    fi
    sleep 0.05
  done
  return 1
}

phase18_cleanup() {
  [[ "${PHASE18_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE18_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring original heater tuning in NVS and runtime after interrupted/incomplete Phase 18 test"
  if phase18_restore_now; then
    log "PASS Phase 18 emergency heater-tuning cleanup restored original runtime/NVS values"
  else
    log "FAIL CRITICAL Phase 18 emergency heater-tuning cleanup could not confirm original runtime values"
  fi
}
phase18_signal_cleanup() { phase18_cleanup; exit 130; }
trap phase18_cleanup EXIT
trap phase18_signal_cleanup INT TERM

log "===== PHASE 0I: RUNTIME HEATING-TUNING HANDOFF (MAIN LOOP) ====="
heat_tuning_json="$(fetch_heating_tuning_state 6 || true)"
heat_hyst_stored_original="$(json_nested_number_field "$heat_tuning_json" stored hysteresisC)"
heat_predict_stored_original="$(json_nested_number_field "$heat_tuning_json" stored predictSeconds)"
heat_min_on_stored_original="$(json_nested_number_field "$heat_tuning_json" stored minOnSeconds)"
heat_min_off_stored_original="$(json_nested_number_field "$heat_tuning_json" stored minOffSeconds)"
heat_hyst_runtime_original="$(json_nested_number_field "$heat_tuning_json" runtime hysteresisC)"
heat_predict_runtime_original="$(json_nested_number_field "$heat_tuning_json" runtime predictSeconds)"
heat_min_on_runtime_original="$(json_nested_number_field "$heat_tuning_json" runtime minOnSeconds)"
heat_min_off_runtime_original="$(json_nested_number_field "$heat_tuning_json" runtime minOffSeconds)"
heat_safe="$(json_bool_field "$heat_tuning_json" safeForAutomatedMutation)"
heat_source="$(json_uint_field "$heat_tuning_json" heatingSource)"

phase18_precheck=1
for v in "$heat_hyst_stored_original" "$heat_predict_stored_original" "$heat_min_on_stored_original" "$heat_min_off_stored_original" \
         "$heat_hyst_runtime_original" "$heat_predict_runtime_original" "$heat_min_on_runtime_original" "$heat_min_off_runtime_original"; do
  [[ -n "$v" ]] || phase18_precheck=0
done

if [[ -z "$heat_tuning_json" ]]; then
  phase18_precheck=0
  log "FAIL heating-tuning precheck: GET /api/settings/heating-tuning returned no valid HTTP 200 JSON after retries"
elif [[ "$heat_safe" != "true" || "$heat_source" != "0" ]]; then
  phase18_precheck=0
  log "SAFETY REFUSAL: heatingSource=${heat_source:-unknown} safeForAutomatedMutation=${heat_safe:-unknown}; temporary heater tuning mutation will not run while heating is configured."
fi

if [[ "$phase18_precheck" -eq 1 ]] && ! float_close "$heat_hyst_stored_original" "$heat_hyst_runtime_original" 0.005; then
  phase18_precheck=0; log "FAIL heating-tuning precheck: stored/runtime hysteresisC already differ"
fi
if [[ "$phase18_precheck" -eq 1 ]] && ! float_close "$heat_predict_stored_original" "$heat_predict_runtime_original" 0.051; then
  phase18_precheck=0; log "FAIL heating-tuning precheck: stored/runtime predictSeconds already differ"
fi
if [[ "$phase18_precheck" -eq 1 && "$heat_min_on_stored_original" != "$heat_min_on_runtime_original" ]]; then
  phase18_precheck=0; log "FAIL heating-tuning precheck: stored/runtime minOnSeconds already differ"
fi
if [[ "$phase18_precheck" -eq 1 && "$heat_min_off_stored_original" != "$heat_min_off_runtime_original" ]]; then
  phase18_precheck=0; log "FAIL heating-tuning precheck: stored/runtime minOffSeconds already differ"
fi

if [[ "$phase18_precheck" -eq 1 ]]; then
  heat_hyst_test="$(awk -v x="$heat_hyst_runtime_original" 'BEGIN{if(x<=1.85) printf "%.2f",x+0.10; else printf "%.2f",x-0.10}')"
  heat_predict_test="$(awk -v x="$heat_predict_runtime_original" 'BEGIN{if(x<=540.0) printf "%.1f",x+30.0; else printf "%.1f",x-30.0}')"
  heat_min_on_test="$(awk -v x="$heat_min_on_runtime_original" 'BEGIN{if(x<=1740) printf "%d",x+30; else printf "%d",x-30}')"
  heat_min_off_test="$(awk -v x="$heat_min_off_runtime_original" 'BEGIN{if(x<=1740) printf "%d",x+30; else printf "%d",x-30}')"

  health_before="$(fetch_async_health 5 || true)"
  heat_req_before="$(json_uint_field "$health_before" heatingTuningWriteRequests)"; heat_req_before="${heat_req_before:-0}"
  heat_fail_before="$(json_uint_field "$health_before" heatingTuningWriteFailures)"; heat_fail_before="${heat_fail_before:-0}"
  heat_applied_before="$(json_uint_field "$health_before" heatingTuningApplied)"; heat_applied_before="${heat_applied_before:-0}"

  log "heater-tuning originals stored/runtime: hyst=$heat_hyst_runtime_original predict=$heat_predict_runtime_original minOn=$heat_min_on_runtime_original minOff=$heat_min_off_runtime_original"
  log "heater-tuning temporary runtime test: hyst=$heat_hyst_test predict=$heat_predict_test minOn=$heat_min_on_test minOff=$heat_min_off_test"
  PHASE18_CLEANUP_ARMED=1

  heat_write_file="${OUTDIR}/heating-tuning-write.json"
  heat_meta="$(post_json_capture "heat-runtime-write" "async-heating-tuning-write" \
    "$BASE8080/api/settings/heating-tuning" \
    "{\"hysteresisC\":$heat_hyst_test,\"predictSeconds\":$heat_predict_test,\"minOnSeconds\":$heat_min_on_test,\"minOffSeconds\":$heat_min_off_test}" \
    "$heat_write_file" 5)"; heat_rc=$?
  IFS=$'\t' read -r heat_http heat_seconds heat_bytes <<< "$heat_meta"
  heat_response="$(cat "$heat_write_file" 2>/dev/null || true)"
  heat_request_id="$(json_uint_field "$heat_response" requestId)"
  if [[ "$heat_rc" -eq 0 && "$heat_http" == "202" && -n "$heat_request_id" && "$heat_response" == *'"applyQueued":true'* ]]; then
    log "PASS heater-tuning POST persisted and queued for main-loop apply: requestId=$heat_request_id"
  else
    log "FAIL heater-tuning POST/queue: HTTP=${heat_http:-000} rc=$heat_rc response='$heat_response'"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi

  if heat_after="$(wait_for_heating_tuning "$heat_hyst_test" "$heat_predict_test" "$heat_min_on_test" "$heat_min_off_test" 60)"; then
    heat_applied_id="$(json_uint_field "$heat_after" lastAppliedId)"
    if [[ "$heat_applied_id" == "$heat_request_id" ]]; then
      log "PASS heater-tuning runtime handoff applied in main loop: requestId=$heat_applied_id"
    else
      log "FAIL heater-tuning applied ID mismatch: queued=$heat_request_id applied=${heat_applied_id:-missing} state='$heat_after'"
      RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
    fi
  else
    log "FAIL heater-tuning runtime values were not observed after main-loop handoff: $heat_after"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi

  heat_state="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
  state_heat_hyst="$(json_number_field "$heat_state" 'settings.heating.hysteresisC')"
  state_heat_predict="$(json_number_field "$heat_state" 'settings.heating.predictSeconds')"
  state_heat_min_on="$(json_number_field "$heat_state" 'settings.heating.minOnSeconds')"
  state_heat_min_off="$(json_number_field "$heat_state" 'settings.heating.minOffSeconds')"
  if [[ -n "$state_heat_hyst" && -n "$state_heat_predict" && -n "$state_heat_min_on" && -n "$state_heat_min_off" ]] && \
     float_close "$state_heat_hyst" "$heat_hyst_test" 0.011 && \
     float_close "$state_heat_predict" "$heat_predict_test" 0.051 && \
     [[ "$state_heat_min_on" == "$heat_min_on_test" && "$state_heat_min_off" == "$heat_min_off_test" ]]; then
    log "PASS /api/state heater-tuning visibility: hyst=$state_heat_hyst predict=$state_heat_predict minOn=$state_heat_min_on minOff=$state_heat_min_off"
  else
    log "FAIL /api/state heater-tuning visibility: hyst=${state_heat_hyst:-missing} predict=${state_heat_predict:-missing} minOn=${state_heat_min_on:-missing} minOff=${state_heat_min_off:-missing}"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi

  heat_bad_meta="$(post_json_capture "negative-test" "async-heating-tuning-invalid" \
    "$BASE8080/api/settings/heating-tuning" \
    '{"hysteresisC":0.01,"predictSeconds":180.0,"minOnSeconds":120,"minOffSeconds":120}' \
    "${OUTDIR}/heating-tuning-invalid.json" 5)"; heat_bad_rc=$?
  IFS=$'\t' read -r heat_bad_http heat_bad_seconds heat_bad_bytes <<< "$heat_bad_meta"
  heat_bad_response="$(cat "${OUTDIR}/heating-tuning-invalid.json" 2>/dev/null || true)"
  if [[ "$heat_bad_rc" -eq 0 && "$heat_bad_http" == "400" && "$heat_bad_response" == *'"heating_tuning_out_of_range"'* ]]; then
    log "PASS invalid heater tuning rejected before persistence/runtime apply"
  else
    log "FAIL invalid heater-tuning handling: HTTP=${heat_bad_http:-000} response='$heat_bad_response'"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi

  if phase18_restore_now; then
    final_heat="$(fetch_heating_tuning_state 6 || true)"
    final_hs="$(json_nested_number_field "$final_heat" stored hysteresisC)"
    final_ps="$(json_nested_number_field "$final_heat" stored predictSeconds)"
    final_mos="$(json_nested_number_field "$final_heat" stored minOnSeconds)"
    final_mfs="$(json_nested_number_field "$final_heat" stored minOffSeconds)"
    final_hr="$(json_nested_number_field "$final_heat" runtime hysteresisC)"
    final_pr="$(json_nested_number_field "$final_heat" runtime predictSeconds)"
    final_mor="$(json_nested_number_field "$final_heat" runtime minOnSeconds)"
    final_mfr="$(json_nested_number_field "$final_heat" runtime minOffSeconds)"
    if float_close "$final_hs" "$heat_hyst_stored_original" 0.005 && \
       float_close "$final_ps" "$heat_predict_stored_original" 0.051 && \
       [[ "$final_mos" == "$heat_min_on_stored_original" && "$final_mfs" == "$heat_min_off_stored_original" ]] && \
       float_close "$final_hr" "$heat_hyst_runtime_original" 0.005 && \
       float_close "$final_pr" "$heat_predict_runtime_original" 0.051 && \
       [[ "$final_mor" == "$heat_min_on_runtime_original" && "$final_mfr" == "$heat_min_off_runtime_original" ]]; then
      PHASE18_CLEANUP_ARMED=0
      log "PASS Phase 18 original heater tuning restored in both NVS and runtime; automatic cleanup disarmed"
    else
      log "FAIL CRITICAL heater-tuning restore verification: $final_heat"
      RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
    fi
  else
    log "FAIL CRITICAL heater-tuning restore request/handoff failed"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"
  heat_req_after="$(json_uint_field "$health_after" heatingTuningWriteRequests)"; heat_req_after="${heat_req_after:-0}"
  heat_fail_after="$(json_uint_field "$health_after" heatingTuningWriteFailures)"; heat_fail_after="${heat_fail_after:-0}"
  heat_applied_after="$(json_uint_field "$health_after" heatingTuningApplied)"; heat_applied_after="${heat_applied_after:-0}"
  if [[ "$heat_req_after" -eq $((heat_req_before+2)) && "$heat_fail_after" -eq "$heat_fail_before" && "$heat_applied_after" -eq $((heat_applied_before+2)) ]]; then
    log "PASS heater-tuning counters: writes $heat_req_before->$heat_req_after applied $heat_applied_before->$heat_applied_after failures $heat_fail_before->$heat_fail_after"
  else
    log "FAIL heater-tuning counters: writes $heat_req_before->$heat_req_after applied $heat_applied_before->$heat_applied_after failures $heat_fail_before->$heat_fail_after"
    RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  fi
else
  RUNTIME_HEAT_FAILURES=$((RUNTIME_HEAT_FAILURES+1))
  log "RUNTIME HEATING-TUNING HANDOFF: NOT EXECUTED because the safety/precondition check failed"
fi

if [[ "$RUNTIME_HEAT_FAILURES" -eq 0 ]]; then
  log "RUNTIME HEATING-TUNING MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME HEATING-TUNING MAIN-LOOP HANDOFF: FAIL ($RUNTIME_HEAT_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 20: heating source/output runtime handoff.
# Automated safety path: run only when heating is currently disabled and the
# runtime Shelly heater has no IP. The temporary source is Shelly (2) with relay
# 0, so no physical ESP relay can energize and the empty Shelly IP prevents an
# outbound switch command. The test restores source 0 immediately afterwards.
# Source=1/ESP-relay is validated by the endpoint but intentionally not automated.
# ---------------------------------------------------------------------------
RUNTIME_HEAT_OUTPUT_FAILURES=0
PHASE20_CLEANUP_ARMED=0

fetch_heating_output_state() {
  local attempts="${1:-6}" i tmp http rc body
  tmp="${OUTDIR}/heating-output-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/settings/heating-output" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN heating-output GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_heating_output() {
  local want_source="$1" want_relay="$2" timeout_steps="${3:-60}"
  local i state rs rr pending
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_heating_output_state 3 || true)"
    rs="$(json_nested_number_field "$state" runtime sourceType)"
    rr="$(json_nested_number_field "$state" runtime relay)"
    pending="$(json_bool_field "$state" pending)"
    if [[ "$rs" == "$want_source" && "$rr" == "$want_relay" && "$pending" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

phase20_restore_now() {
  local response_file="${OUTDIR}/heating-output-emergency-restore.json"
  local attempt state meta rc http seconds bytes
  for attempt in $(seq 1 20); do
    meta="$(post_json_capture "heat-output-runtime-restore" "async-heating-output-emergency-restore" \
      "$BASE8080/api/settings/heating-output" \
      "{\"sourceType\":$heat_output_source_stored_original,\"relay\":$heat_output_relay_stored_original}" \
      "$response_file" 5)"; rc=$?
    IFS=$'\t' read -r http seconds bytes <<< "$meta"
    if [[ "$rc" -eq 0 && "$http" == "202" ]]; then
      if state="$(wait_for_heating_output "$heat_output_source_runtime_original" "$heat_output_relay_runtime_original" 60)"; then
        return 0
      fi
    elif [[ "$http" == "409" ]]; then
      sleep 0.05
      continue
    fi
    sleep 0.05
  done
  return 1
}

phase20_cleanup() {
  [[ "${PHASE20_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE20_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring original heating source/output after interrupted/incomplete Phase 20 test"
  if phase20_restore_now; then
    log "PASS Phase 20 emergency heating-output cleanup restored original runtime/NVS values"
  else
    log "FAIL CRITICAL Phase 20 emergency heating-output cleanup could not confirm original runtime values"
  fi
}
phase20_signal_cleanup() { phase20_cleanup; exit 130; }
trap phase20_cleanup EXIT
trap phase20_signal_cleanup INT TERM

log "===== PHASE 0J: RUNTIME HEATING SOURCE/OUTPUT HANDOFF (MAIN LOOP) ====="
heat_output_json="$(fetch_heating_output_state 6 || true)"
heat_output_source_stored_original="$(json_nested_number_field "$heat_output_json" stored sourceType)"
heat_output_relay_stored_original="$(json_nested_number_field "$heat_output_json" stored relay)"
heat_output_source_runtime_original="$(json_nested_number_field "$heat_output_json" runtime sourceType)"
heat_output_relay_runtime_original="$(json_nested_number_field "$heat_output_json" runtime relay)"
heat_output_safe="$(json_bool_field "$heat_output_json" safeForAutomatedMutation)"
heat_output_shelly_configured="$(json_bool_field "$heat_output_json" shellyHeaterConfigured)"

phase20_precheck=1
for v in "$heat_output_source_stored_original" "$heat_output_relay_stored_original" \
         "$heat_output_source_runtime_original" "$heat_output_relay_runtime_original"; do
  [[ -n "$v" ]] || phase20_precheck=0
done
if [[ "$heat_output_source_stored_original" != "0" || "$heat_output_relay_stored_original" != "0" || \
      "$heat_output_source_runtime_original" != "0" || "$heat_output_relay_runtime_original" != "0" || \
      "$heat_output_safe" != "true" || "$heat_output_shelly_configured" != "false" ]]; then
  phase20_precheck=0
fi

if [[ "$phase20_precheck" -eq 1 ]]; then
  log "heating-output originals stored/runtime: source=$heat_output_source_stored_original/$heat_output_source_runtime_original relay=$heat_output_relay_stored_original/$heat_output_relay_runtime_original"
  log "heating-output temporary safe test: source=2 (Shelly), relay=0, runtime Shelly heater unconfigured"

  health_before="$(fetch_async_health 5 || true)"
  heat_output_req_before="$(json_uint_field "$health_before" heatingOutputWriteRequests)"; heat_output_req_before="${heat_output_req_before:-0}"
  heat_output_fail_before="$(json_uint_field "$health_before" heatingOutputWriteFailures)"; heat_output_fail_before="${heat_output_fail_before:-0}"
  heat_output_applied_before="$(json_uint_field "$health_before" heatingOutputApplied)"; heat_output_applied_before="${heat_output_applied_before:-0}"
  heat_output_apply_fail_before="$(json_uint_field "$health_before" heatingOutputApplyFailures)"; heat_output_apply_fail_before="${heat_output_apply_fail_before:-0}"
  heat_output_rollback_fail_before="$(json_uint_field "$health_before" heatingOutputRollbackFailures)"; heat_output_rollback_fail_before="${heat_output_rollback_fail_before:-0}"

  PHASE20_CLEANUP_ARMED=1
  heat_output_write_file="${OUTDIR}/heating-output-write.json"
  heat_output_meta="$(post_json_capture "heat-output-runtime-write" "async-heating-output-write" \
    "$BASE8080/api/settings/heating-output" '{"sourceType":2,"relay":0}' \
    "$heat_output_write_file" 5)"; heat_output_rc=$?
  IFS=$'\t' read -r heat_output_http heat_output_seconds heat_output_bytes <<< "$heat_output_meta"
  heat_output_response="$(cat "$heat_output_write_file" 2>/dev/null || true)"
  heat_output_request_id="$(json_uint_field "$heat_output_response" requestId)"
  if [[ "$heat_output_rc" -eq 0 && "$heat_output_http" == "202" && -n "$heat_output_request_id" && "$heat_output_response" == *'"applyQueued":true'* ]]; then
    log "PASS heating-output POST persisted and queued for main-loop apply: requestId=$heat_output_request_id"
  else
    log "FAIL heating-output POST/queue: HTTP=${heat_output_http:-000} rc=$heat_output_rc response='$heat_output_response'"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi

  if heat_output_after="$(wait_for_heating_output 2 0 60)"; then
    heat_output_applied_id="$(json_uint_field "$heat_output_after" lastAppliedId)"
    if [[ "$heat_output_applied_id" == "$heat_output_request_id" ]]; then
      log "PASS heating-output runtime handoff applied in main loop: requestId=$heat_output_applied_id"
    else
      log "FAIL heating-output applied ID mismatch: queued=$heat_output_request_id applied=${heat_output_applied_id:-missing} state='$heat_output_after'"
      RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
    fi
  else
    log "FAIL heating-output runtime values were not observed after main-loop handoff: $heat_output_after"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi

  heat_output_state="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
  state_heat_source="$(json_number_field "$heat_output_state" 'settings.heating.sourceType')"
  state_heat_relay="$(json_number_field "$heat_output_state" 'settings.heatingrelay')"
  if [[ "$state_heat_source" == "2" && "$state_heat_relay" == "0" ]]; then
    log "PASS /api/state heating-output visibility: sourceType=$state_heat_source relay=$state_heat_relay"
  else
    log "FAIL /api/state heating-output visibility: sourceType=${state_heat_source:-missing} relay=${state_heat_relay:-missing}"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi

  heat_output_bad_meta="$(post_json_capture "negative-test" "async-heating-output-invalid" \
    "$BASE8080/api/settings/heating-output" '{"sourceType":1,"relay":0}' \
    "${OUTDIR}/heating-output-invalid.json" 5)"; heat_output_bad_rc=$?
  IFS=$'\t' read -r heat_output_bad_http heat_output_bad_seconds heat_output_bad_bytes <<< "$heat_output_bad_meta"
  heat_output_bad_response="$(cat "${OUTDIR}/heating-output-invalid.json" 2>/dev/null || true)"
  if [[ "$heat_output_bad_rc" -eq 0 && "$heat_output_bad_http" == "400" && "$heat_output_bad_response" == *'"heating_output_out_of_range"'* ]]; then
    log "PASS invalid ESP-heater relay configuration rejected before persistence/runtime apply"
  else
    log "FAIL invalid heating-output handling: HTTP=${heat_output_bad_http:-000} response='$heat_output_bad_response'"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi

  if phase20_restore_now; then
    final_heat_output="$(fetch_heating_output_state 6 || true)"
    final_hos="$(json_nested_number_field "$final_heat_output" stored sourceType)"
    final_hor="$(json_nested_number_field "$final_heat_output" stored relay)"
    final_hors="$(json_nested_number_field "$final_heat_output" runtime sourceType)"
    final_horr="$(json_nested_number_field "$final_heat_output" runtime relay)"
    if [[ "$final_hos" == "$heat_output_source_stored_original" && \
          "$final_hor" == "$heat_output_relay_stored_original" && \
          "$final_hors" == "$heat_output_source_runtime_original" && \
          "$final_horr" == "$heat_output_relay_runtime_original" ]]; then
      PHASE20_CLEANUP_ARMED=0
      log "PASS Phase 20 original heating source/output restored in both NVS and runtime; automatic cleanup disarmed"
    else
      log "FAIL CRITICAL heating-output restore verification: $final_heat_output"
      RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
    fi
  else
    log "FAIL CRITICAL heating-output restore request/handoff failed"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"
  heat_output_req_after="$(json_uint_field "$health_after" heatingOutputWriteRequests)"; heat_output_req_after="${heat_output_req_after:-0}"
  heat_output_fail_after="$(json_uint_field "$health_after" heatingOutputWriteFailures)"; heat_output_fail_after="${heat_output_fail_after:-0}"
  heat_output_applied_after="$(json_uint_field "$health_after" heatingOutputApplied)"; heat_output_applied_after="${heat_output_applied_after:-0}"
  heat_output_apply_fail_after="$(json_uint_field "$health_after" heatingOutputApplyFailures)"; heat_output_apply_fail_after="${heat_output_apply_fail_after:-0}"
  heat_output_rollback_fail_after="$(json_uint_field "$health_after" heatingOutputRollbackFailures)"; heat_output_rollback_fail_after="${heat_output_rollback_fail_after:-0}"
  if [[ "$heat_output_req_after" -eq $((heat_output_req_before+2)) && \
        "$heat_output_fail_after" -eq "$heat_output_fail_before" && \
        "$heat_output_applied_after" -eq $((heat_output_applied_before+2)) && \
        "$heat_output_apply_fail_after" -eq "$heat_output_apply_fail_before" && \
        "$heat_output_rollback_fail_after" -eq "$heat_output_rollback_fail_before" ]]; then
    log "PASS heating-output counters: writes $heat_output_req_before->$heat_output_req_after applied $heat_output_applied_before->$heat_output_applied_after writeFailures $heat_output_fail_before->$heat_output_fail_after applyFailures $heat_output_apply_fail_before->$heat_output_apply_fail_after rollbackFailures $heat_output_rollback_fail_before->$heat_output_rollback_fail_after"
  else
    log "FAIL heating-output counters: writes $heat_output_req_before->$heat_output_req_after applied $heat_output_applied_before->$heat_output_applied_after writeFailures $heat_output_fail_before->$heat_output_fail_after applyFailures $heat_output_apply_fail_before->$heat_output_apply_fail_after rollbackFailures $heat_output_rollback_fail_before->$heat_output_rollback_fail_after"
    RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  fi
else
  RUNTIME_HEAT_OUTPUT_FAILURES=$((RUNTIME_HEAT_OUTPUT_FAILURES+1))
  log "SAFETY REFUSAL: Phase 20 automated runtime mutation requires stored/runtime source=0 relay=0 and an unconfigured runtime Shelly heater. State='$heat_output_json'"
fi

if [[ "$RUNTIME_HEAT_OUTPUT_FAILURES" -eq 0 ]]; then
  log "RUNTIME HEATING SOURCE/OUTPUT MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME HEATING SOURCE/OUTPUT MAIN-LOOP HANDOFF: FAIL ($RUNTIME_HEAT_OUTPUT_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 21: grow-light ON time + photoperiod runtime handoff.
# Safety rule: automated mutation runs only while the runtime Shelly light IP is
# empty. Therefore the main-loop handoff changes only RAM/NVS during the test and
# cannot reprogram or switch a real grow light. With a configured Shelly light,
# the test performs read/validation checks and reports a safe SKIP for mutation.
# ---------------------------------------------------------------------------
RUNTIME_LIGHT_FAILURES=0
PHASE21_CLEANUP_ARMED=0

fetch_light_schedule_state() {
  local attempts="${1:-6}" i tmp http rc body
  tmp="${OUTDIR}/light-schedule-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/settings/light-schedule" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN light-schedule GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_light_schedule() {
  local want_time="$1" want_hours="$2" timeout_steps="${3:-80}"
  local i state rt rh pending
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_light_schedule_state 3 || true)"
    rt="$(json_nested_string_field "$state" runtime onTime)"
    rh="$(json_nested_number_field "$state" runtime dayHours)"
    pending="$(json_bool_field "$state" pending)"
    if [[ "$rt" == "$want_time" && "$rh" == "$want_hours" && "$pending" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

phase21_restore_now() {
  local response_file="${OUTDIR}/light-schedule-emergency-restore.json"
  local attempt state meta rc http seconds bytes escaped
  escaped="$(json_escape_string "$light_on_stored_original")"
  for attempt in $(seq 1 20); do
    meta="$(post_json_capture "light-runtime-restore" "async-light-schedule-emergency-restore" \
      "$BASE8080/api/settings/light-schedule" \
      "{\"onTime\":\"$escaped\",\"dayHours\":$light_hours_stored_original}" \
      "$response_file" 5)"; rc=$?
    IFS=$'\t' read -r http seconds bytes <<< "$meta"
    if [[ "$rc" -eq 0 && "$http" == "202" ]]; then
      if state="$(wait_for_light_schedule "$light_on_runtime_original" "$light_hours_runtime_original" 80)"; then
        return 0
      fi
    elif [[ "$http" == "409" ]]; then
      sleep 0.05
      continue
    fi
    sleep 0.05
  done
  return 1
}

phase21_cleanup() {
  [[ "${PHASE21_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE21_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring original grow-light schedule after interrupted/incomplete Phase 21 test"
  if phase21_restore_now; then
    log "PASS Phase 21 emergency light-schedule cleanup restored original runtime/NVS values"
  else
    log "FAIL CRITICAL Phase 21 emergency light-schedule cleanup could not confirm original runtime values"
  fi
}
phase21_signal_cleanup() { phase21_cleanup; exit 130; }
trap phase21_cleanup EXIT
trap phase21_signal_cleanup INT TERM

log "===== PHASE 0K: RUNTIME GROW-LIGHT SCHEDULE HANDOFF (MAIN LOOP) ====="
light_schedule_json="$(fetch_light_schedule_state 6 || true)"
light_on_stored_original="$(json_nested_string_field "$light_schedule_json" stored onTime)"
light_hours_stored_original="$(json_nested_number_field "$light_schedule_json" stored dayHours)"
light_on_runtime_original="$(json_nested_string_field "$light_schedule_json" runtime onTime)"
light_hours_runtime_original="$(json_nested_number_field "$light_schedule_json" runtime dayHours)"
light_safe="$(json_bool_field "$light_schedule_json" safeForAutomatedMutation)"
light_shelly_configured="$(json_bool_field "$light_schedule_json" shellyLightConfigured)"

phase21_precheck=1
for v in "$light_on_stored_original" "$light_hours_stored_original" \
         "$light_on_runtime_original" "$light_hours_runtime_original"; do
  [[ -n "$v" ]] || phase21_precheck=0
done

if [[ "$phase21_precheck" -eq 1 ]]; then
  log "light-schedule originals stored/runtime: on=$light_on_stored_original/$light_on_runtime_original hours=$light_hours_stored_original/$light_hours_runtime_original shellyConfigured=$light_shelly_configured"

  # Negative validation is always safe, even on a controller with a configured light.
  light_bad_meta="$(post_json_capture "negative-test" "async-light-schedule-invalid" \
    "$BASE8080/api/settings/light-schedule" '{"onTime":"25:00","dayHours":18}' \
    "${OUTDIR}/light-schedule-invalid.json" 5)"; light_bad_rc=$?
  IFS=$'\t' read -r light_bad_http light_bad_seconds light_bad_bytes <<< "$light_bad_meta"
  light_bad_response="$(cat "${OUTDIR}/light-schedule-invalid.json" 2>/dev/null || true)"
  if [[ "$light_bad_rc" -eq 0 && "$light_bad_http" == "400" && "$light_bad_response" == *'"light_schedule_out_of_range"'* ]]; then
    log "PASS invalid grow-light schedule rejected before persistence/runtime apply"
  else
    log "FAIL invalid grow-light schedule handling: HTTP=${light_bad_http:-000} response='$light_bad_response'"
    RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
  fi

  if [[ "$light_safe" == "true" && "$light_shelly_configured" == "false" ]]; then
    # Pick a deterministic, valid temporary schedule different from the common default.
    light_test_on="07:15"
    light_test_hours=17
    if [[ "$light_on_runtime_original" == "$light_test_on" && "$light_hours_runtime_original" == "$light_test_hours" ]]; then
      light_test_on="08:20"
      light_test_hours=16
    fi
    log "light-schedule temporary safe test: on=$light_test_on hours=$light_test_hours; runtime Shelly light unconfigured"

    health_before="$(fetch_async_health 5 || true)"
    light_req_before="$(json_uint_field "$health_before" lightScheduleWriteRequests)"; light_req_before="${light_req_before:-0}"
    light_fail_before="$(json_uint_field "$health_before" lightScheduleWriteFailures)"; light_fail_before="${light_fail_before:-0}"
    light_applied_before="$(json_uint_field "$health_before" lightScheduleApplied)"; light_applied_before="${light_applied_before:-0}"
    light_apply_fail_before="$(json_uint_field "$health_before" lightScheduleApplyFailures)"; light_apply_fail_before="${light_apply_fail_before:-0}"
    light_rollback_fail_before="$(json_uint_field "$health_before" lightScheduleRollbackFailures)"; light_rollback_fail_before="${light_rollback_fail_before:-0}"

    PHASE21_CLEANUP_ARMED=1
    light_write_file="${OUTDIR}/light-schedule-write.json"
    light_meta="$(post_json_capture "light-runtime-write" "async-light-schedule-write" \
      "$BASE8080/api/settings/light-schedule" \
      "{\"onTime\":\"$light_test_on\",\"dayHours\":$light_test_hours}" \
      "$light_write_file" 5)"; light_rc=$?
    IFS=$'\t' read -r light_http light_seconds light_bytes <<< "$light_meta"
    light_response="$(cat "$light_write_file" 2>/dev/null || true)"
    light_request_id="$(json_uint_field "$light_response" requestId)"
    if [[ "$light_rc" -eq 0 && "$light_http" == "202" && -n "$light_request_id" && "$light_response" == *'"applyQueued":true'* ]]; then
      log "PASS light-schedule POST persisted and queued for main-loop apply: requestId=$light_request_id"
    else
      log "FAIL light-schedule POST/queue: HTTP=${light_http:-000} rc=$light_rc response='$light_response'"
      RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
    fi

    if light_after="$(wait_for_light_schedule "$light_test_on" "$light_test_hours" 80)"; then
      light_applied_id="$(json_uint_field "$light_after" lastAppliedId)"
      if [[ "$light_applied_id" == "$light_request_id" ]]; then
        log "PASS light-schedule runtime handoff applied in main loop: requestId=$light_applied_id"
      else
        log "FAIL light-schedule applied ID mismatch: queued=$light_request_id applied=${light_applied_id:-missing} state='$light_after'"
        RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
      fi
    else
      log "FAIL light-schedule runtime values were not observed after main-loop handoff: $light_after"
      RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
    fi

    light_state="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
    state_light_on="$(json_string_field "$light_state" 'settings.grow.lightOnTime')"
    state_light_hours="$(json_number_field "$light_state" 'settings.grow.lightDayHours')"
    if [[ "$state_light_on" == "$light_test_on" && "$state_light_hours" == "$light_test_hours" ]]; then
      log "PASS /api/state light-schedule visibility: on=$state_light_on dayHours=$state_light_hours"
    else
      log "FAIL /api/state light-schedule visibility: on=${state_light_on:-missing} dayHours=${state_light_hours:-missing}"
      RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
    fi

    if phase21_restore_now; then
      final_light="$(fetch_light_schedule_state 6 || true)"
      final_los="$(json_nested_string_field "$final_light" stored onTime)"
      final_lhs="$(json_nested_number_field "$final_light" stored dayHours)"
      final_lor="$(json_nested_string_field "$final_light" runtime onTime)"
      final_lhr="$(json_nested_number_field "$final_light" runtime dayHours)"
      if [[ "$final_los" == "$light_on_stored_original" && \
            "$final_lhs" == "$light_hours_stored_original" && \
            "$final_lor" == "$light_on_runtime_original" && \
            "$final_lhr" == "$light_hours_runtime_original" ]]; then
        PHASE21_CLEANUP_ARMED=0
        log "PASS Phase 21 original grow-light schedule restored in both NVS and runtime; automatic cleanup disarmed"
      else
        log "FAIL CRITICAL light-schedule restore verification: $final_light"
        RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
      fi
    else
      log "FAIL CRITICAL light-schedule restore request/handoff failed"
      RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
    fi

    health_after="$(fetch_async_health 5 || true)"
    light_req_after="$(json_uint_field "$health_after" lightScheduleWriteRequests)"; light_req_after="${light_req_after:-0}"
    light_fail_after="$(json_uint_field "$health_after" lightScheduleWriteFailures)"; light_fail_after="${light_fail_after:-0}"
    light_applied_after="$(json_uint_field "$health_after" lightScheduleApplied)"; light_applied_after="${light_applied_after:-0}"
    light_apply_fail_after="$(json_uint_field "$health_after" lightScheduleApplyFailures)"; light_apply_fail_after="${light_apply_fail_after:-0}"
    light_rollback_fail_after="$(json_uint_field "$health_after" lightScheduleRollbackFailures)"; light_rollback_fail_after="${light_rollback_fail_after:-0}"
    if [[ "$light_req_after" -eq $((light_req_before+2)) && \
          "$light_fail_after" -eq "$light_fail_before" && \
          "$light_applied_after" -eq $((light_applied_before+2)) && \
          "$light_apply_fail_after" -eq "$light_apply_fail_before" && \
"$light_rollback_fail_after" -eq "$light_rollback_fail_before" ]]; then
      log "PASS light-schedule counters: writes $light_req_before->$light_req_after applied $light_applied_before->$light_applied_after writeFailures $light_fail_before->$light_fail_after applyFailures $light_apply_fail_before->$light_apply_fail_after rollbackFailures $light_rollback_fail_before->$light_rollback_fail_after"
    else
      log "FAIL light-schedule counters: writes $light_req_before->$light_req_after applied $light_applied_before->$light_applied_after writeFailures $light_fail_before->$light_fail_after applyFailures $light_apply_fail_before->$light_apply_fail_after rollbackFailures $light_rollback_fail_before->$light_rollback_fail_after"
      RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
    fi
  else
    log "SAFE SKIP: Phase 21 runtime mutation not executed because a Shelly light is configured. Read/validation path tested only. State='$light_schedule_json'"
  fi
else
  RUNTIME_LIGHT_FAILURES=$((RUNTIME_LIGHT_FAILURES+1))
  log "FAIL Phase 21 precheck: could not read complete stored/runtime light schedule state: '$light_schedule_json'"
fi

if [[ "$RUNTIME_LIGHT_FAILURES" -eq 0 ]]; then
  log "RUNTIME GROW-LIGHT SCHEDULE MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME GROW-LIGHT SCHEDULE MAIN-LOOP HANDOFF: FAIL ($RUNTIME_LIGHT_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 22: ESP relay minute-schedule runtime handoff.
# Safety rule: automated mutation is performed only on an ACTIVE relay whose
# schedule is disabled in BOTH stored and runtime state. The test keeps enabled
# false for the temporary value and restore, so it cannot command a relay ON.
# ---------------------------------------------------------------------------
RUNTIME_RELAY_SCHEDULE_FAILURES=0
PHASE22_CLEANUP_ARMED=0
relay_schedule_test_relay=0

fetch_relay_schedule_state() {
  local relay="$1" attempts="${2:-6}" i tmp http rc body
  tmp="${OUTDIR}/relay-schedule-${relay}-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/settings/relay-schedule/$relay" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN relay-schedule/$relay GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_relay_schedule() {
  local relay="$1" want_enabled="$2" want_ilo="$3" want_on="$4" want_off="$5" timeout_steps="${6:-80}"
  local i state re ri ro rf pending
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_relay_schedule_state "$relay" 3 || true)"
    re="$(json_nested_bool_field "$state" runtime enabled)"
    ri="$(json_nested_bool_field "$state" runtime ifLightOff)"
    ro="$(json_nested_number_field "$state" runtime onMin)"
    rf="$(json_nested_number_field "$state" runtime offMin)"
    pending="$(json_bool_field "$state" pending)"
    if [[ "$re" == "$want_enabled" && "$ri" == "$want_ilo" && \
          "$ro" == "$want_on" && "$rf" == "$want_off" && "$pending" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

phase22_restore_now() {
  local response_file="${OUTDIR}/relay-schedule-emergency-restore.json"
  local attempt state meta rc http seconds bytes
  [[ "$relay_schedule_test_relay" -gt 0 ]] || return 0
  for attempt in $(seq 1 20); do
    meta="$(post_json_capture "relay-schedule-restore" "async-relay-schedule-emergency-restore" \
      "$BASE8080/api/settings/relay-schedule/$relay_schedule_test_relay" \
      "{\"enabled\":$relay_enabled_stored_original,\"ifLightOff\":$relay_ilo_stored_original,\"onMin\":$relay_on_stored_original,\"offMin\":$relay_off_stored_original}" \
      "$response_file" 5)"; rc=$?
    IFS=$'\t' read -r http seconds bytes <<< "$meta"
    if [[ "$rc" -eq 0 && "$http" == "202" ]]; then
      if state="$(wait_for_relay_schedule "$relay_schedule_test_relay" \
        "$relay_enabled_runtime_original" "$relay_ilo_runtime_original" \
        "$relay_on_runtime_original" "$relay_off_runtime_original" 80)"; then
        return 0
      fi
    elif [[ "$http" == "409" ]]; then
      sleep 0.05
      continue
    fi
    sleep 0.05
  done
  return 1
}

phase22_cleanup() {
  [[ "${PHASE22_CLEANUP_ARMED:-0}" -eq 1 ]] || return 0
  PHASE22_CLEANUP_ARMED=0
  log "SAFETY CLEANUP: restoring original disabled relay schedule after interrupted/incomplete Phase 22 test"
  if phase22_restore_now; then
    log "PASS Phase 22 emergency relay-schedule cleanup restored original runtime/NVS values"
  else
    log "FAIL CRITICAL Phase 22 emergency relay-schedule cleanup could not confirm original runtime values"
  fi
}
phase22_signal_cleanup() { phase22_cleanup; exit 130; }
trap phase22_cleanup EXIT
trap phase22_signal_cleanup INT TERM

log "===== PHASE 0L: RUNTIME RELAY-SCHEDULE HANDOFF (MAIN LOOP) ====="

# Negative validation is safe: relay 1 is active on every supported 4/8-relay build.
relay_bad_meta="$(post_json_capture "negative-test" "async-relay-schedule-invalid" \
  "$BASE8080/api/settings/relay-schedule/1" \
  '{"enabled":false,"ifLightOff":false,"onMin":60,"offMin":0}' \
  "${OUTDIR}/relay-schedule-invalid.json" 5)"; relay_bad_rc=$?
IFS=$'\t' read -r relay_bad_http relay_bad_seconds relay_bad_bytes <<< "$relay_bad_meta"
relay_bad_response="$(cat "${OUTDIR}/relay-schedule-invalid.json" 2>/dev/null || true)"
if [[ "$relay_bad_rc" -eq 0 && "$relay_bad_http" == "400" && "$relay_bad_response" == *'"relay_schedule_out_of_range"'* ]]; then
  log "PASS invalid relay schedule rejected before persistence/runtime apply"
else
  log "FAIL invalid relay schedule handling: HTTP=${relay_bad_http:-000} response='$relay_bad_response'"
  RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
fi

# Find the first active relay whose schedule is disabled in stored + runtime state.
relay_schedule_json=""
for relay_candidate in 1 2 3 4 5; do
  candidate_state="$(fetch_relay_schedule_state "$relay_candidate" 4 || true)"
  candidate_active="$(json_bool_field "$candidate_state" active)"
  candidate_safe="$(json_bool_field "$candidate_state" safeForAutomatedMutation)"
  if [[ "$candidate_active" == "true" && "$candidate_safe" == "true" ]]; then
    relay_schedule_test_relay="$relay_candidate"
    relay_schedule_json="$candidate_state"
    break
  fi
done

if [[ "$relay_schedule_test_relay" -gt 0 ]]; then
  relay_enabled_stored_original="$(json_nested_bool_field "$relay_schedule_json" stored enabled)"
  relay_ilo_stored_original="$(json_nested_bool_field "$relay_schedule_json" stored ifLightOff)"
  relay_on_stored_original="$(json_nested_number_field "$relay_schedule_json" stored onMin)"
  relay_off_stored_original="$(json_nested_number_field "$relay_schedule_json" stored offMin)"
  relay_enabled_runtime_original="$(json_nested_bool_field "$relay_schedule_json" runtime enabled)"
  relay_ilo_runtime_original="$(json_nested_bool_field "$relay_schedule_json" runtime ifLightOff)"
  relay_on_runtime_original="$(json_nested_number_field "$relay_schedule_json" runtime onMin)"
  relay_off_runtime_original="$(json_nested_number_field "$relay_schedule_json" runtime offMin)"

  phase22_precheck=1
  for v in "$relay_enabled_stored_original" "$relay_ilo_stored_original" \
           "$relay_on_stored_original" "$relay_off_stored_original" \
           "$relay_enabled_runtime_original" "$relay_ilo_runtime_original" \
           "$relay_on_runtime_original" "$relay_off_runtime_original"; do
    [[ -n "$v" ]] || phase22_precheck=0
  done

  if [[ "$phase22_precheck" -eq 1 && "$relay_enabled_stored_original" == "false" && "$relay_enabled_runtime_original" == "false" ]]; then
    log "relay-schedule safe target relay=$relay_schedule_test_relay stored/runtime: enabled=$relay_enabled_stored_original/$relay_enabled_runtime_original ifLightOff=$relay_ilo_stored_original/$relay_ilo_runtime_original on=$relay_on_stored_original/$relay_on_runtime_original off=$relay_off_stored_original/$relay_off_runtime_original"

    relay_test_enabled=false
    if [[ "$relay_ilo_runtime_original" == "true" ]]; then relay_test_ilo=false; else relay_test_ilo=true; fi
    relay_test_on=11
    relay_test_off=41
    if [[ "$relay_on_runtime_original" == "$relay_test_on" && "$relay_off_runtime_original" == "$relay_test_off" ]]; then
      relay_test_on=12
      relay_test_off=42
    fi
    log "relay-schedule temporary SAFE test: relay=$relay_schedule_test_relay enabled=false ifLightOff=$relay_test_ilo onMin=$relay_test_on offMin=$relay_test_off"

    health_before="$(fetch_async_health 5 || true)"
    relay_sched_req_before="$(json_uint_field "$health_before" relayScheduleWriteRequests)"; relay_sched_req_before="${relay_sched_req_before:-0}"
    relay_sched_fail_before="$(json_uint_field "$health_before" relayScheduleWriteFailures)"; relay_sched_fail_before="${relay_sched_fail_before:-0}"
    relay_sched_applied_before="$(json_uint_field "$health_before" relayScheduleApplied)"; relay_sched_applied_before="${relay_sched_applied_before:-0}"
    relay_sched_apply_fail_before="$(json_uint_field "$health_before" relayScheduleApplyFailures)"; relay_sched_apply_fail_before="${relay_sched_apply_fail_before:-0}"
    relay_sched_rollback_fail_before="$(json_uint_field "$health_before" relayScheduleRollbackFailures)"; relay_sched_rollback_fail_before="${relay_sched_rollback_fail_before:-0}"

    relay_write_meta="$(post_json_capture "relay-schedule-write" "async-relay-schedule-write" \
      "$BASE8080/api/settings/relay-schedule/$relay_schedule_test_relay" \
      "{\"enabled\":false,\"ifLightOff\":$relay_test_ilo,\"onMin\":$relay_test_on,\"offMin\":$relay_test_off}" \
      "${OUTDIR}/relay-schedule-write.json" 5)"; relay_write_rc=$?
    IFS=$'\t' read -r relay_write_http relay_write_seconds relay_write_bytes <<< "$relay_write_meta"
    relay_write_response="$(cat "${OUTDIR}/relay-schedule-write.json" 2>/dev/null || true)"
    relay_request_id="$(json_uint_field "$relay_write_response" requestId)"

    if [[ "$relay_write_rc" -eq 0 && "$relay_write_http" == "202" && -n "$relay_request_id" ]]; then
      PHASE22_CLEANUP_ARMED=1
      log "PASS relay-schedule POST persisted and queued for main-loop apply: relay=$relay_schedule_test_relay requestId=$relay_request_id"
    else
      log "FAIL relay-schedule POST: HTTP=${relay_write_http:-000} response='$relay_write_response'"
      RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
    fi

    if relay_after="$(wait_for_relay_schedule "$relay_schedule_test_relay" false "$relay_test_ilo" "$relay_test_on" "$relay_test_off" 80)"; then
      log "PASS relay-schedule runtime handoff applied in main loop: relay=$relay_schedule_test_relay requestId=$relay_request_id"
    else
      log "FAIL relay-schedule runtime handoff timeout/state: '$relay_after'"
      RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
    fi

    relay_state="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
    relay_idx=$((relay_schedule_test_relay-1))
    if [[ "$relay_state" == *"\"relays[$relay_idx].schedule.enabled\":false"* && \
          "$relay_state" == *"\"relays[$relay_idx].schedule.ifLightOff\":$relay_test_ilo"* && \
          "$relay_state" == *"\"relays[$relay_idx].schedule.start\":$relay_test_on"* && \
          "$relay_state" == *"\"relays[$relay_idx].schedule.end\":$relay_test_off"* ]]; then
      log "PASS /api/state relay-schedule visibility: relay=$relay_schedule_test_relay enabled=false ifLightOff=$relay_test_ilo onMin=$relay_test_on offMin=$relay_test_off"
    else
      log "FAIL /api/state relay-schedule visibility for relay=$relay_schedule_test_relay"
      RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
    fi

    if phase22_restore_now; then
      final_relay="$(fetch_relay_schedule_state "$relay_schedule_test_relay" 6 || true)"
      final_se="$(json_nested_bool_field "$final_relay" stored enabled)"
      final_si="$(json_nested_bool_field "$final_relay" stored ifLightOff)"
      final_so="$(json_nested_number_field "$final_relay" stored onMin)"
      final_sf="$(json_nested_number_field "$final_relay" stored offMin)"
      final_re="$(json_nested_bool_field "$final_relay" runtime enabled)"
      final_ri="$(json_nested_bool_field "$final_relay" runtime ifLightOff)"
      final_ro="$(json_nested_number_field "$final_relay" runtime onMin)"
      final_rf="$(json_nested_number_field "$final_relay" runtime offMin)"
      if [[ "$final_se" == "$relay_enabled_stored_original" && "$final_si" == "$relay_ilo_stored_original" && \
            "$final_so" == "$relay_on_stored_original" && "$final_sf" == "$relay_off_stored_original" && \
            "$final_re" == "$relay_enabled_runtime_original" && "$final_ri" == "$relay_ilo_runtime_original" && \
            "$final_ro" == "$relay_on_runtime_original" && "$final_rf" == "$relay_off_runtime_original" ]]; then
        PHASE22_CLEANUP_ARMED=0
        log "PASS Phase 22 original relay schedule restored in both NVS and runtime; automatic cleanup disarmed"
      else
        log "FAIL CRITICAL relay-schedule restore verification: $final_relay"
        RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
      fi
    else
      log "FAIL CRITICAL relay-schedule restore request/handoff failed"
      RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
    fi

    health_after="$(fetch_async_health 5 || true)"
    relay_sched_req_after="$(json_uint_field "$health_after" relayScheduleWriteRequests)"; relay_sched_req_after="${relay_sched_req_after:-0}"
    relay_sched_fail_after="$(json_uint_field "$health_after" relayScheduleWriteFailures)"; relay_sched_fail_after="${relay_sched_fail_after:-0}"
    relay_sched_applied_after="$(json_uint_field "$health_after" relayScheduleApplied)"; relay_sched_applied_after="${relay_sched_applied_after:-0}"
    relay_sched_apply_fail_after="$(json_uint_field "$health_after" relayScheduleApplyFailures)"; relay_sched_apply_fail_after="${relay_sched_apply_fail_after:-0}"
    relay_sched_rollback_fail_after="$(json_uint_field "$health_after" relayScheduleRollbackFailures)"; relay_sched_rollback_fail_after="${relay_sched_rollback_fail_after:-0}"
    if [[ "$relay_sched_req_after" -eq $((relay_sched_req_before+2)) && \
          "$relay_sched_fail_after" -eq "$relay_sched_fail_before" && \
          "$relay_sched_applied_after" -eq $((relay_sched_applied_before+2)) && \
          "$relay_sched_apply_fail_after" -eq "$relay_sched_apply_fail_before" && \
          "$relay_sched_rollback_fail_after" -eq "$relay_sched_rollback_fail_before" ]]; then
      log "PASS relay-schedule counters: writes $relay_sched_req_before->$relay_sched_req_after applied $relay_sched_applied_before->$relay_sched_applied_after writeFailures $relay_sched_fail_before->$relay_sched_fail_after applyFailures $relay_sched_apply_fail_before->$relay_sched_apply_fail_after rollbackFailures $relay_sched_rollback_fail_before->$relay_sched_rollback_fail_after"
    else
      log "FAIL relay-schedule counters: writes $relay_sched_req_before->$relay_sched_req_after applied $relay_sched_applied_before->$relay_sched_applied_after writeFailures $relay_sched_fail_before->$relay_sched_fail_after applyFailures $relay_sched_apply_fail_before->$relay_sched_apply_fail_after rollbackFailures $relay_sched_rollback_fail_before->$relay_sched_rollback_fail_after"
      RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
    fi
  else
    log "FAIL Phase 22 precheck for selected safe relay=$relay_schedule_test_relay: '$relay_schedule_json'"
    RUNTIME_RELAY_SCHEDULE_FAILURES=$((RUNTIME_RELAY_SCHEDULE_FAILURES+1))
  fi
else
  log "SAFE SKIP: no active relay with stored/runtime schedule disabled; invalid-value and GET safety paths tested without changing relay control"
fi

if [[ "$RUNTIME_RELAY_SCHEDULE_FAILURES" -eq 0 ]]; then
  log "RUNTIME RELAY-SCHEDULE MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME RELAY-SCHEDULE MAIN-LOOP HANDOFF: FAIL ($RUNTIME_RELAY_SCHEDULE_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 23: manual relay action handoff.
# Safety rule: automated hardware coverage uses ONLY an explicit OFF request on
# an active relay that is already OFF, has no enabled schedule, is not assigned
# as the ESP heater output, and reports safeForAutomatedOffNoop=true. The real
# loop->setRelay()->GPIO path is executed, but the pin remains LOW throughout.
# The dashboard /relay/N/toggle route is registered from the same queue engine;
# it is intentionally not auto-fired because any real toggle could energize load.
# ---------------------------------------------------------------------------
RUNTIME_RELAY_ACTION_FAILURES=0
relay_action_test_relay=0

fetch_relay_action_state() {
  local relay="$1" attempts="${2:-6}" i tmp http rc body
  tmp="${OUTDIR}/relay-action-${relay}-get.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/relay/$relay/state" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    log "WARN relay-action/$relay GET attempt $i/$attempts failed: HTTP=${http:-000} rc=$rc" >&2
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_relay_action_apply() {
  local relay="$1" request_id="$2" timeout_steps="${3:-80}"
  local i state pending applied state_value
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_relay_action_state "$relay" 3 || true)"
    pending="$(json_bool_field "$state" pending)"
    applied="$(json_uint_field "$state" lastAppliedId)"
    state_value="$(json_bool_field "$state" state)"
    if [[ "$pending" == "false" && "$applied" == "$request_id" && "$state_value" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

log "===== PHASE 0M: RUNTIME RELAY ACTION HANDOFF (MAIN LOOP, SAFE OFF->OFF) ====="
relay_action_bad_meta="$(post_json_capture "negative-test" "async-relay-action-invalid" \
  "$BASE8080/api/relay/1/state" '{"state":"off"}' \
  "${OUTDIR}/relay-action-invalid.json" 5)"; relay_action_bad_rc=$?
IFS=$'\t' read -r relay_action_bad_http relay_action_bad_seconds relay_action_bad_bytes <<< "$relay_action_bad_meta"
relay_action_bad_response="$(cat "${OUTDIR}/relay-action-invalid.json" 2>/dev/null || true)"
if [[ "$relay_action_bad_rc" -eq 0 && "$relay_action_bad_http" == "400" && "$relay_action_bad_response" == *'"err":"invalid_relay_state"'* ]]; then
  log "PASS invalid relay state rejected before queue/hardware apply"
else
  log "FAIL invalid relay state rejection: HTTP=${relay_action_bad_http:-000} response='$relay_action_bad_response'"
  RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
fi

for relay in 1 2 3 4 5; do
  candidate="$(fetch_relay_action_state "$relay" 3 || true)"
  candidate_safe="$(json_bool_field "$candidate" safeForAutomatedOffNoop)"
  candidate_pending="$(json_bool_field "$candidate" pending)"
  if [[ "$candidate_safe" == "true" && "$candidate_pending" == "false" ]]; then
    relay_action_test_relay="$relay"
    break
  fi
done

if [[ "$relay_action_test_relay" -gt 0 ]]; then
  relay_action_before="$(fetch_relay_action_state "$relay_action_test_relay" 6 || true)"
  log "relay-action safe target relay=$relay_action_test_relay state=$(json_bool_field "$relay_action_before" state) scheduleEnabled=$(json_bool_field "$relay_action_before" scheduleEnabled) heaterAssigned=$(json_bool_field "$relay_action_before" heaterAssigned)"

  health_before="$(fetch_async_health 5 || true)"
  relay_action_req_before="$(json_uint_field "$health_before" relayActionRequests)"; relay_action_req_before="${relay_action_req_before:-0}"
  relay_action_fail_before="$(json_uint_field "$health_before" relayActionFailures)"; relay_action_fail_before="${relay_action_fail_before:-0}"
  relay_action_queued_before="$(json_uint_field "$health_before" relayActionQueued)"; relay_action_queued_before="${relay_action_queued_before:-0}"
  relay_action_applied_before="$(json_uint_field "$health_before" relayActionApplied)"; relay_action_applied_before="${relay_action_applied_before:-0}"
  relay_action_apply_fail_before="$(json_uint_field "$health_before" relayActionApplyFailures)"; relay_action_apply_fail_before="${relay_action_apply_fail_before:-0}"
  relay_action_transitions_before="$(json_uint_field "$health_before" relayActionTransitions)"; relay_action_transitions_before="${relay_action_transitions_before:-0}"
  relay_action_noops_before="$(json_uint_field "$health_before" relayActionNoops)"; relay_action_noops_before="${relay_action_noops_before:-0}"
  relay_action_state_gen_before="$(json_uint_field "$health_before" stateGeneration)"; relay_action_state_gen_before="${relay_action_state_gen_before:-0}"

  relay_action_meta="$(post_json_capture "relay-action-write" "async-relay-action-safe-off" \
    "$BASE8080/api/relay/$relay_action_test_relay/state" '{"state":false,"noTransitionOnly":true}' \
    "${OUTDIR}/relay-action-safe-off.json" 5)"; relay_action_rc=$?
  IFS=$'\t' read -r relay_action_http relay_action_seconds relay_action_bytes <<< "$relay_action_meta"
  relay_action_response="$(cat "${OUTDIR}/relay-action-safe-off.json" 2>/dev/null || true)"
  relay_action_request_id="$(json_uint_field "$relay_action_response" requestId)"

  if [[ "$relay_action_rc" -eq 0 && "$relay_action_http" == "202" && -n "$relay_action_request_id" && "$relay_action_response" == *'"applyQueued":true'* ]]; then
    log "PASS relay-action explicit OFF queued for main-loop apply: relay=$relay_action_test_relay requestId=$relay_action_request_id"
  else
    log "FAIL relay-action queue: HTTP=${relay_action_http:-000} response='$relay_action_response'"
    RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
  fi

  if [[ -n "$relay_action_request_id" ]]; then
    if relay_action_after="$(wait_for_relay_action_apply "$relay_action_test_relay" "$relay_action_request_id" 32)"; then
      log "PASS relay-action request-id acknowledgement completed inside dashboard 1.6s budget with relay still OFF: relay=$relay_action_test_relay requestId=$relay_action_request_id"
    else
      log "FAIL relay-action main-loop apply timeout/state: '$relay_action_after'"
      RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
    fi
  fi

  relay_action_state_json="$(curl -sS --max-time 5 "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
  relay_action_idx=$((relay_action_test_relay-1))
  relay_action_state_key="relays[$relay_action_idx].state"
  if [[ "$relay_action_state_json" == *"\"relays[$relay_action_idx].state\":false"* ]]; then
    log "PASS /api/state relay-action visibility: relay=$relay_action_test_relay remains OFF"
  else
    log "FAIL /api/state relay-action visibility: key=$relay_action_state_key not confirmed false"
    RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
  fi

  health_after="$(fetch_async_health 5 || true)"
  relay_action_req_after="$(json_uint_field "$health_after" relayActionRequests)"; relay_action_req_after="${relay_action_req_after:-0}"
  relay_action_fail_after="$(json_uint_field "$health_after" relayActionFailures)"; relay_action_fail_after="${relay_action_fail_after:-0}"
  relay_action_queued_after="$(json_uint_field "$health_after" relayActionQueued)"; relay_action_queued_after="${relay_action_queued_after:-0}"
  relay_action_applied_after="$(json_uint_field "$health_after" relayActionApplied)"; relay_action_applied_after="${relay_action_applied_after:-0}"
  relay_action_apply_fail_after="$(json_uint_field "$health_after" relayActionApplyFailures)"; relay_action_apply_fail_after="${relay_action_apply_fail_after:-0}"
  relay_action_transitions_after="$(json_uint_field "$health_after" relayActionTransitions)"; relay_action_transitions_after="${relay_action_transitions_after:-0}"
  relay_action_noops_after="$(json_uint_field "$health_after" relayActionNoops)"; relay_action_noops_after="${relay_action_noops_after:-0}"
  relay_action_state_gen_after="$(json_uint_field "$health_after" stateGeneration)"; relay_action_state_gen_after="${relay_action_state_gen_after:-0}"
  relay_action_cache_gen_after="$(json_uint_field "$health_after" stateCacheGeneration)"; relay_action_cache_gen_after="${relay_action_cache_gen_after:-0}"

  if [[ "$relay_action_state_gen_after" -gt "$relay_action_state_gen_before" ]]; then
    log "PASS relay-action invalidated Async state generation: $relay_action_state_gen_before -> $relay_action_state_gen_after (cacheGeneration=$relay_action_cache_gen_after)"
  else
    log "FAIL relay-action state generation did not advance: $relay_action_state_gen_before -> $relay_action_state_gen_after"
    RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
  fi

  if [[ "$relay_action_req_after" -eq $((relay_action_req_before+1)) && \
        "$relay_action_fail_after" -eq "$relay_action_fail_before" && \
        "$relay_action_queued_after" -eq $((relay_action_queued_before+1)) && \
        "$relay_action_applied_after" -eq $((relay_action_applied_before+1)) && \
        "$relay_action_apply_fail_after" -eq "$relay_action_apply_fail_before" && \
        "$relay_action_transitions_after" -eq "$relay_action_transitions_before" && \
        "$relay_action_noops_after" -eq $((relay_action_noops_before+1)) ]]; then
    log "PASS relay-action counters prove safe OFF->OFF no-op: requests $relay_action_req_before->$relay_action_req_after applied $relay_action_applied_before->$relay_action_applied_after transitions $relay_action_transitions_before->$relay_action_transitions_after noops $relay_action_noops_before->$relay_action_noops_after"
  else
    log "FAIL relay-action counters: requests $relay_action_req_before->$relay_action_req_after failures $relay_action_fail_before->$relay_action_fail_after queued $relay_action_queued_before->$relay_action_queued_after applied $relay_action_applied_before->$relay_action_applied_after applyFailures $relay_action_apply_fail_before->$relay_action_apply_fail_after transitions $relay_action_transitions_before->$relay_action_transitions_after noops $relay_action_noops_before->$relay_action_noops_after"
    RUNTIME_RELAY_ACTION_FAILURES=$((RUNTIME_RELAY_ACTION_FAILURES+1))
  fi
else
  log "SAFE SKIP: no active relay is simultaneously OFF, unscheduled and not assigned as heater; no automatic GPIO write performed"
fi

if [[ "$RUNTIME_RELAY_ACTION_FAILURES" -eq 0 ]]; then
  log "RUNTIME RELAY ACTION MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME RELAY ACTION MAIN-LOOP HANDOFF: FAIL ($RUNTIME_RELAY_ACTION_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 24: irrigation/Flood handoff.
# Safety rule: never start watering and never trigger a pump automatically.
# The only runtime mutation is a guarded STOP while irrigation is already idle
# and all pump relays report OFF. The main-loop stop path is exercised without
# any electrical transition. Flood coverage uses only an invalid/disabled token,
# proving the permanent-port route exists without changing Flood state.
# ---------------------------------------------------------------------------
RUNTIME_WATERING_FAILURES=0

fetch_watering_state() {
  local attempts="${1:-6}" i tmp http rc body
  tmp="${OUTDIR}/watering-state.tmp"
  for i in $(seq 1 "$attempts"); do
    http="$(curl -sS --http1.1 --max-time 5 -o "$tmp" -w '%{http_code}' \
      "$BASE8080/api/watering/state" 2>>"$LOGFILE")"
    rc=$?
    body="$(cat "$tmp" 2>/dev/null || true)"
    if [[ "$rc" -eq 0 && "$http" == "200" && "$body" == *'"ok":true'* ]]; then
      rm -f "$tmp"
      printf '%s' "$body"
      return 0
    fi
    sleep 0.10
  done
  rm -f "$tmp"
  return 1
}

wait_for_watering_apply() {
  local request_id="$1" timeout_steps="${2:-80}" i state pending applied running anypump
  state=""
  for i in $(seq 1 "$timeout_steps"); do
    state="$(fetch_watering_state 3 || true)"
    pending="$(json_bool_field "$state" pending)"
    applied="$(json_uint_field "$state" lastAppliedId)"
    running="$(json_bool_field "$state" running)"
    anypump="$(json_bool_field "$state" anyPumpOn)"
    if [[ "$pending" == "false" && "$applied" == "$request_id" && "$running" == "false" && "$anypump" == "false" ]]; then
      printf '%s' "$state"
      return 0
    fi
    sleep 0.05
  done
  printf '%s' "$state"
  return 1
}

log "===== PHASE 0N: IRRIGATION/FLOOD RUNTIME HANDOFF (SAFE IDLE STOP) ====="
watering_bad_meta="$(post_json_capture "negative-test" "async-watering-invalid" \
  "$BASE8080/api/watering/action" '{"action":"invalid"}' \
  "${OUTDIR}/watering-invalid.json" 5)"; watering_bad_rc=$?
IFS=$'\t' read -r watering_bad_http watering_bad_seconds watering_bad_bytes <<< "$watering_bad_meta"
watering_bad_response="$(cat "${OUTDIR}/watering-invalid.json" 2>/dev/null || true)"
if [[ "$watering_bad_rc" -eq 0 && "$watering_bad_http" == "400" && "$watering_bad_response" == *'"err":"invalid_watering_action"'* ]]; then
  log "PASS invalid watering action rejected before runtime apply"
else
  log "FAIL invalid watering action rejection: HTTP=${watering_bad_http:-000} response='$watering_bad_response'"
  RUNTIME_WATERING_FAILURES=$((RUNTIME_WATERING_FAILURES+1))
fi

watering_state_before="$(fetch_watering_state 6 || true)"
watering_safe="$(json_bool_field "$watering_state_before" safeForAutomatedStopNoop)"
if [[ "$watering_safe" == "true" ]]; then
  health_before="$(fetch_async_health 5 || true)"
  water_req_before="$(json_uint_field "$health_before" wateringRequests)"; water_req_before="${water_req_before:-0}"
  water_fail_before="$(json_uint_field "$health_before" wateringFailures)"; water_fail_before="${water_fail_before:-0}"
  water_queued_before="$(json_uint_field "$health_before" wateringQueued)"; water_queued_before="${water_queued_before:-0}"
  water_applied_before="$(json_uint_field "$health_before" wateringApplied)"; water_applied_before="${water_applied_before:-0}"
  water_apply_fail_before="$(json_uint_field "$health_before" wateringApplyFailures)"; water_apply_fail_before="${water_apply_fail_before:-0}"
  water_noops_before="$(json_uint_field "$health_before" wateringNoops)"; water_noops_before="${water_noops_before:-0}"
  water_starts_before="$(json_uint_field "$health_before" wateringStarts)"; water_starts_before="${water_starts_before:-0}"
  water_pulses_before="$(json_uint_field "$health_before" wateringPumpPulses)"; water_pulses_before="${water_pulses_before:-0}"

  watering_meta="$(post_json_capture "watering-action" "async-watering-safe-stop" \
    "$BASE8080/api/watering/action" '{"action":"stop","noTransitionOnly":true}' \
    "${OUTDIR}/watering-safe-stop.json" 5)"; watering_rc=$?
  IFS=$'\t' read -r watering_http watering_seconds watering_bytes <<< "$watering_meta"
  watering_response="$(cat "${OUTDIR}/watering-safe-stop.json" 2>/dev/null || true)"
  watering_request_id="$(json_uint_field "$watering_response" requestId)"
  if [[ "$watering_rc" -eq 0 && "$watering_http" == "202" && -n "$watering_request_id" && "$watering_response" == *'"applyQueued":true'* ]]; then
    log "PASS guarded idle STOP queued for main-loop apply: requestId=$watering_request_id"
  else
    log "FAIL watering stop queue: HTTP=${watering_http:-000} response='$watering_response'"
    RUNTIME_WATERING_FAILURES=$((RUNTIME_WATERING_FAILURES+1))
  fi

  if [[ -n "$watering_request_id" ]]; then
    if watering_state_after="$(wait_for_watering_apply "$watering_request_id" 80)"; then
      log "PASS guarded idle STOP applied with watering idle and all pumps OFF: requestId=$watering_request_id"
    else
      log "FAIL watering idle STOP apply timeout/state: '$watering_state_after'"
      RUNTIME_WATERING_FAILURES=$((RUNTIME_WATERING_FAILURES+1))
    fi
  fi

  health_after="$(fetch_async_health 5 || true)"
  water_req_after="$(json_uint_field "$health_after" wateringRequests)"; water_req_after="${water_req_after:-0}"
  water_fail_after="$(json_uint_field "$health_after" wateringFailures)"; water_fail_after="${water_fail_after:-0}"
  water_queued_after="$(json_uint_field "$health_after" wateringQueued)"; water_queued_after="${water_queued_after:-0}"
  water_applied_after="$(json_uint_field "$health_after" wateringApplied)"; water_applied_after="${water_applied_after:-0}"
  water_apply_fail_after="$(json_uint_field "$health_after" wateringApplyFailures)"; water_apply_fail_after="${water_apply_fail_after:-0}"
  water_noops_after="$(json_uint_field "$health_after" wateringNoops)"; water_noops_after="${water_noops_after:-0}"
  water_starts_after="$(json_uint_field "$health_after" wateringStarts)"; water_starts_after="${water_starts_after:-0}"
  water_pulses_after="$(json_uint_field "$health_after" wateringPumpPulses)"; water_pulses_after="${water_pulses_after:-0}"

  if [[ "$water_req_after" -eq $((water_req_before+1)) && \
        "$water_fail_after" -eq "$water_fail_before" && \
        "$water_queued_after" -eq $((water_queued_before+1)) && \
        "$water_applied_after" -eq $((water_applied_before+1)) && \
        "$water_apply_fail_after" -eq "$water_apply_fail_before" && \
        "$water_noops_after" -eq $((water_noops_before+1)) && \
        "$water_starts_after" -eq "$water_starts_before" && \
        "$water_pulses_after" -eq "$water_pulses_before" ]]; then
    log "PASS watering counters prove safe idle STOP no-op; no start and no pump pulse occurred"
  else
    log "FAIL watering counters: requests $water_req_before->$water_req_after failures $water_fail_before->$water_fail_after queued $water_queued_before->$water_queued_after applied $water_applied_before->$water_applied_after applyFailures $water_apply_fail_before->$water_apply_fail_after noops $water_noops_before->$water_noops_after starts $water_starts_before->$water_starts_after pulses $water_pulses_before->$water_pulses_after"
    RUNTIME_WATERING_FAILURES=$((RUNTIME_WATERING_FAILURES+1))
  fi
else
  log "SAFE SKIP watering runtime mutation: irrigation is running or at least one pump is ON/armed"
fi

# Flood webhook route existence/auth check only; never send the real token.
flood_health_before="$(fetch_async_health 5 || true)"
flood_queued_before="$(json_uint_field "$flood_health_before" floodEventQueued)"; flood_queued_before="${flood_queued_before:-0}"
flood_http="$(curl -sS --max-time 5 -o "${OUTDIR}/flood-invalid-token.json" -w '%{http_code}' \
  "$BASE8080/api/flood/event?alarm=1" 2>>"$LOGFILE" || true)"
flood_response="$(cat "${OUTDIR}/flood-invalid-token.json" 2>/dev/null || true)"
flood_health_after="$(fetch_async_health 5 || true)"
flood_queued_after="$(json_uint_field "$flood_health_after" floodEventQueued)"; flood_queued_after="${flood_queued_after:-0}"
if [[ ( "$flood_http" == "403" || "$flood_http" == "404" ) && "$flood_queued_after" -eq "$flood_queued_before" ]]; then
  log "PASS Flood webhook permanent-port route rejects invalid/disabled token without queueing state mutation: HTTP=$flood_http"
else
  log "FAIL Flood webhook safe auth check: HTTP=${flood_http:-000} response='$flood_response' queued $flood_queued_before->$flood_queued_after"
  RUNTIME_WATERING_FAILURES=$((RUNTIME_WATERING_FAILURES+1))
fi

if [[ "$RUNTIME_WATERING_FAILURES" -eq 0 ]]; then
  log "RUNTIME IRRIGATION/FLOOD MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME IRRIGATION/FLOOD MAIN-LOOP HANDOFF: FAIL ($RUNTIME_WATERING_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 25 queued storage regression. No real diary mutation is permitted here.
# A reserved impossible ID is submitted with noMutationOnly=true. The main loop
# must prove the ID is absent and count an applied no-op. The CSV bytes are
# compared before/after to prove the automated test did not alter LittleFS data.
# ---------------------------------------------------------------------------
RUNTIME_STORAGE_FAILURES=0

wait_for_storage_apply() {
  local request_id="$1" timeout_steps="${2:-160}" i body state
  body=""
  for i in $(seq 1 "$timeout_steps"); do
    body="$(curl -sS --max-time 3 "$BASE8080/api/storage/action-status?requestId=$request_id" 2>>"$LOGFILE" || true)"
    state="$(json_string_field "$body" state)"
    if [[ "$state" == "applied" ]]; then printf '%s' "$body"; return 0; fi
    if [[ "$state" == "failed" ]]; then printf '%s' "$body"; return 2; fi
    sleep 0.05
  done
  printf '%s' "$body"
  return 1
}

log "===== PHASE 0O: DIARY/LITTLEFS MAIN-LOOP HANDOFF (SAFE NO-MUTATION PROBE) ====="

storage_bad_add_meta="$(post_json_capture "negative-test" "async-storage-invalid-add" \
  "$BASE8080/api/diary/add" '{"note":"x","phase":"invalid"}' \
  "${OUTDIR}/storage-invalid-add.json" 5)"; storage_bad_add_rc=$?
IFS=$'\t' read -r storage_bad_add_http storage_bad_add_seconds storage_bad_add_bytes <<< "$storage_bad_add_meta"
storage_bad_add_response="$(cat "${OUTDIR}/storage-invalid-add.json" 2>/dev/null || true)"
if [[ "$storage_bad_add_rc" -eq 0 && "$storage_bad_add_http" == "400" && "$storage_bad_add_response" == *'"err":"invalid_diary_add"'* ]]; then
  log "PASS invalid diary add rejected before queue/LittleFS mutation"
else
  log "FAIL invalid diary add handling: HTTP=${storage_bad_add_http:-000} response='$storage_bad_add_response'"
  RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
fi

storage_bad_format_meta="$(post_json_capture "negative-test" "async-storage-invalid-format" \
  "$BASE8080/api/fs/format" '{"confirm":"NO"}' \
  "${OUTDIR}/storage-invalid-format.json" 5)"; storage_bad_format_rc=$?
IFS=$'\t' read -r storage_bad_format_http storage_bad_format_seconds storage_bad_format_bytes <<< "$storage_bad_format_meta"
storage_bad_format_response="$(cat "${OUTDIR}/storage-invalid-format.json" 2>/dev/null || true)"
if [[ "$storage_bad_format_rc" -eq 0 && "$storage_bad_format_http" == "403" && "$storage_bad_format_response" == *'"confirmation_required"'* ]]; then
  log "PASS LittleFS format requires explicit FORMAT_LITTLEFS confirmation"
else
  log "FAIL LittleFS format confirmation guard: HTTP=${storage_bad_format_http:-000} response='$storage_bad_format_response'"
  RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
fi

storage_before_csv="${OUTDIR}/storage-before.csv"
storage_after_csv="${OUTDIR}/storage-after.csv"
curl -sS --max-time 5 "$BASE8080/api/diary.csv" -o "$storage_before_csv" 2>>"$LOGFILE" || true
health_before="$(fetch_async_health 5 || true)"
storage_req_before="$(json_uint_field "$health_before" storageRequests)"; storage_req_before="${storage_req_before:-0}"
storage_fail_before="$(json_uint_field "$health_before" storageFailures)"; storage_fail_before="${storage_fail_before:-0}"
storage_queued_before="$(json_uint_field "$health_before" storageQueued)"; storage_queued_before="${storage_queued_before:-0}"
storage_applied_before="$(json_uint_field "$health_before" storageApplied)"; storage_applied_before="${storage_applied_before:-0}"
storage_apply_fail_before="$(json_uint_field "$health_before" storageApplyFailures)"; storage_apply_fail_before="${storage_apply_fail_before:-0}"
storage_noops_before="$(json_uint_field "$health_before" storageNoops)"; storage_noops_before="${storage_noops_before:-0}"

storage_probe_meta="$(post_json_capture "storage-action" "async-storage-safe-delete-noop" \
  "$BASE8080/api/diary/delete" '{"id":"__ASYNC_PHASE25_PROBE__","noMutationOnly":true}' \
  "${OUTDIR}/storage-safe-delete.json" 5)"; storage_probe_rc=$?
IFS=$'\t' read -r storage_probe_http storage_probe_seconds storage_probe_bytes <<< "$storage_probe_meta"
storage_probe_response="$(cat "${OUTDIR}/storage-safe-delete.json" 2>/dev/null || true)"
storage_request_id="$(json_uint_field "$storage_probe_response" requestId)"
if [[ "$storage_probe_rc" -eq 0 && "$storage_probe_http" == "202" && -n "$storage_request_id" && "$storage_probe_response" == *'"queued":true'* ]]; then
  log "PASS guarded diary delete no-op queued for main-loop apply: requestId=$storage_request_id"
else
  log "FAIL storage no-op queue: HTTP=${storage_probe_http:-000} response='$storage_probe_response'"
  RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
fi

if [[ -n "$storage_request_id" ]]; then
  storage_status_after="$(wait_for_storage_apply "$storage_request_id" 160)"; storage_wait_rc=$?
  if [[ "$storage_wait_rc" -eq 0 && "$(json_string_field "$storage_status_after" state)" == "applied" ]]; then
    log "PASS guarded diary delete completed as main-loop no-op: requestId=$storage_request_id"
  else
    log "FAIL storage no-op apply/status: rc=$storage_wait_rc status='$storage_status_after'"
    RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
  fi
fi

curl -sS --max-time 5 "$BASE8080/api/diary.csv" -o "$storage_after_csv" 2>>"$LOGFILE" || true
if cmp -s "$storage_before_csv" "$storage_after_csv"; then
  log "PASS diary CSV is byte-identical before/after automated Phase-25 no-op"
else
  log "FAIL diary CSV changed during supposedly non-mutating Phase-25 probe"
  RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
fi

health_after="$(fetch_async_health 5 || true)"
storage_req_after="$(json_uint_field "$health_after" storageRequests)"; storage_req_after="${storage_req_after:-0}"
storage_fail_after="$(json_uint_field "$health_after" storageFailures)"; storage_fail_after="${storage_fail_after:-0}"
storage_queued_after="$(json_uint_field "$health_after" storageQueued)"; storage_queued_after="${storage_queued_after:-0}"
storage_applied_after="$(json_uint_field "$health_after" storageApplied)"; storage_applied_after="${storage_applied_after:-0}"
storage_apply_fail_after="$(json_uint_field "$health_after" storageApplyFailures)"; storage_apply_fail_after="${storage_apply_fail_after:-0}"
storage_noops_after="$(json_uint_field "$health_after" storageNoops)"; storage_noops_after="${storage_noops_after:-0}"
if [[ "$storage_req_after" -eq $((storage_req_before+1)) && \
      "$storage_fail_after" -eq "$storage_fail_before" && \
      "$storage_queued_after" -eq $((storage_queued_before+1)) && \
      "$storage_applied_after" -eq $((storage_applied_before+1)) && \
      "$storage_apply_fail_after" -eq "$storage_apply_fail_before" && \
      "$storage_noops_after" -eq $((storage_noops_before+1)) ]]; then
  log "PASS storage counters prove safe queued main-loop no-op; no LittleFS mutation occurred"
else
  log "FAIL storage counters: requests $storage_req_before->$storage_req_after failures $storage_fail_before->$storage_fail_after queued $storage_queued_before->$storage_queued_after applied $storage_applied_before->$storage_applied_after applyFailures $storage_apply_fail_before->$storage_apply_fail_after noops $storage_noops_before->$storage_noops_after"
  RUNTIME_STORAGE_FAILURES=$((RUNTIME_STORAGE_FAILURES+1))
fi

if [[ "$RUNTIME_STORAGE_FAILURES" -eq 0 ]]; then
  log "RUNTIME DIARY/LITTLEFS MAIN-LOOP HANDOFF: PASS"
else
  log "RUNTIME DIARY/LITTLEFS MAIN-LOOP HANDOFF: FAIL ($RUNTIME_STORAGE_FAILURES problem(s))"
fi
log ""

# ---------------------------------------------------------------------------
# Phase 25 compact state/stats transport regression.
# v0.2.85 replaces the unknown-length chunked state/stats response with a
# callback response that advertises Content-Length up front. Keep this short: 40
# parallel rounds catch incomplete/empty-body regressions without turning the
# normal staged-migration test back into a long diagnostic run.
# ---------------------------------------------------------------------------
TRANSPORT_ROUNDS="${GT_TRANSPORT_ROUNDS:-40}"
TRANSPORT_MAX_TIME="${GT_TRANSPORT_MAX_TIME:-4}"
TRANSPORT_REQUEST_FAILURES=0
TRANSPORT_TIMEOUTS=0
TRANSPORT_SLOW_EVENTS=0
TRANSPORT_HEADER_FAILURES=0
SLOW_THRESHOLD="${GT_SLOW_THRESHOLD:-1.0}"

float_ge() {
  awk -v a="$1" -v b="$2" 'BEGIN { exit !(a+0 >= b+0) }'
}

transport_request_to_file() {
  local url="$1" max_time="$2" outfile="$3" headerfile="$4"
  curl --http1.1 -sS -o "${outfile}.body" -D "$headerfile" --max-time "$max_time" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' \
    "$url" >"$outfile" 2>>"$LOGFILE"
  printf '%s' "$?" >"${outfile}.rc"
}

transport_header_value() {
  local headerfile="$1" name="$2"
  awk -v n="$name" 'BEGIN{IGNORECASE=1} index(tolower($0),tolower(n) ":")==1 {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$headerfile"
}

log "===== PHASE 0P: SHARED-CACHE DIRECT-BUFFER FIXED-LENGTH STATE/STATS REGRESSION ($TRANSPORT_ROUNDS parallel rounds) ====="
baseline_headers="${OUTDIR}/transport-baseline-state.headers"
baseline_body="${OUTDIR}/transport-baseline-state.json"
baseline_meta="$(curl --http1.1 -sS --max-time "$TRANSPORT_MAX_TIME" -D "$baseline_headers" -o "$baseline_body" \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/api/state" 2>>"$LOGFILE" || true)"
baseline_transport="$(transport_header_value "$baseline_headers" X-GrowTent-State-Transport)"
baseline_block="$(transport_header_value "$baseline_headers" X-GrowTent-State-Block-Max)"
baseline_source_len="$(transport_header_value "$baseline_headers" X-GrowTent-State-Source-Length)"
baseline_format="$(transport_header_value "$baseline_headers" X-GrowTent-State-Format)"
baseline_te="$(transport_header_value "$baseline_headers" Transfer-Encoding)"
baseline_cl="$(transport_header_value "$baseline_headers" Content-Length)"
IFS=$'\t' read -r baseline_http baseline_total baseline_bytes <<< "$baseline_meta"
if [[ "$baseline_http" == "200" && "$baseline_transport" == "shared-cache-direct-buffer-fixed-length" && "$baseline_block" == "0" && "${baseline_cl:-0}" -ge 5000 && "$baseline_source_len" == "$baseline_cl" && "$baseline_format" == "compact" && -z "$baseline_te" && "${baseline_bytes:-0}" == "$baseline_cl" ]]; then
  log "PASS shared-cache direct-buffer fixed-length baseline: HTTP=$baseline_http bytes=$baseline_bytes contentLength=$baseline_cl sourceLength=$baseline_source_len format=$baseline_format transport=$baseline_transport blockMax=$baseline_block"
else
  TRANSPORT_HEADER_FAILURES=$((TRANSPORT_HEADER_FAILURES + 1))
  log "FAIL shared-cache direct-buffer fixed-length baseline: HTTP=${baseline_http:-000} bytes=${baseline_bytes:-0} contentLength='${baseline_cl:-missing}' sourceLength='${baseline_source_len:-missing}' format='${baseline_format:-missing}' transport='${baseline_transport:-missing}' blockMax='${baseline_block:-missing}' transferEncoding='${baseline_te:-none}'"
fi

for i in $(seq 1 "$TRANSPORT_ROUNDS"); do
  fs="${OUTDIR}/transport-${i}-state.tmp"
  ft="${OUTDIR}/transport-${i}-stats.tmp"
  hs="${OUTDIR}/transport-${i}-state.headers"
  ht="${OUTDIR}/transport-${i}-stats.headers"
  transport_request_to_file "$BASE8080/api/state" "$TRANSPORT_MAX_TIME" "$fs" "$hs" & ps=$!
  transport_request_to_file "$BASE8080/api/stats" "$TRANSPORT_MAX_TIME" "$ft" "$ht" & pt=$!
  wait "$ps" "$pt" 2>/dev/null || true

  keep_transport_round=0
  for ep in state stats; do
    outfile="$fs"; headerfile="$hs"
    [[ "$ep" == "stats" ]] && outfile="$ft" && headerfile="$ht"
    result="$(cat "$outfile" 2>/dev/null || true)"
    rc="$(cat "${outfile}.rc" 2>/dev/null || echo 99)"
    IFS=$'\t' read -r http total bytes <<< "$result"
    http="${http:-000}"; total="${total:-0}"; bytes="${bytes:-0}"
    record_result "phase15-transport" "async-$ep" "${http}"$'\t'"${total}"$'\t'"${bytes}" "$rc"
    transport="$(transport_header_value "$headerfile" X-GrowTent-State-Transport)"
    block="$(transport_header_value "$headerfile" X-GrowTent-State-Block-Max)"
    content_length="$(transport_header_value "$headerfile" Content-Length)"
    source_length="$(transport_header_value "$headerfile" X-GrowTent-State-Source-Length)"
    payload_format="$(transport_header_value "$headerfile" X-GrowTent-State-Format)"
    transfer_encoding="$(transport_header_value "$headerfile" Transfer-Encoding)"
    if [[ "$transport" != "shared-cache-direct-buffer-fixed-length" || "$block" != "0" || "${content_length:-0}" -lt 5000 || "$source_length" != "$content_length" || "$payload_format" != "compact" || -n "$transfer_encoding" ]]; then
      TRANSPORT_HEADER_FAILURES=$((TRANSPORT_HEADER_FAILURES + 1))
      keep_transport_round=1
      log "FAIL transport headers round=$i endpoint=$ep transport='${transport:-missing}' blockMax='${block:-missing}' contentLength='${content_length:-missing}' sourceLength='${source_length:-missing}' format='${payload_format:-missing}' transferEncoding='${transfer_encoding:-none}'"
    fi
    if [[ "$rc" -ne 0 || "$http" != "200" || "$bytes" -lt 5000 || "$bytes" != "${content_length:-0}" ]]; then
      TRANSPORT_REQUEST_FAILURES=$((TRANSPORT_REQUEST_FAILURES + 1))
      keep_transport_round=1
      [[ "$rc" -eq 28 || "$http" == "000" ]] && TRANSPORT_TIMEOUTS=$((TRANSPORT_TIMEOUTS + 1))
      log "FAIL transport round=$i endpoint=$ep HTTP=$http bytes=$bytes total=${total}s rc=$rc"
    elif float_ge "$total" "$SLOW_THRESHOLD"; then
      TRANSPORT_SLOW_EVENTS=$((TRANSPORT_SLOW_EVENTS + 1))
    fi
  done
  if [[ "$keep_transport_round" -eq 0 ]]; then
    rm -f "$fs" "${fs}.rc" "${fs}.body" "$ft" "${ft}.rc" "${ft}.body" "$hs" "$ht"
  else
    log "transport failure artifacts preserved for round=$i: ${fs}.body $hs ${ft}.body $ht"
  fi
  if (( i % 10 == 0 )); then
    log "transport progress: $i/$TRANSPORT_ROUNDS failures=$TRANSPORT_REQUEST_FAILURES timeouts=$TRANSPORT_TIMEOUTS slow=$TRANSPORT_SLOW_EVENTS"
  fi
  sleep 0.1
done

if [[ "$TRANSPORT_REQUEST_FAILURES" -eq 0 && "$TRANSPORT_HEADER_FAILURES" -eq 0 ]]; then
  log "SHARED-CACHE DIRECT-BUFFER FIXED-LENGTH STATE/STATS REGRESSION: PASS (rounds=$TRANSPORT_ROUNDS timeouts=0)"
else
  log "SHARED-CACHE DIRECT-BUFFER FIXED-LENGTH STATE/STATS REGRESSION: FAIL (failures=$TRANSPORT_REQUEST_FAILURES timeouts=$TRANSPORT_TIMEOUTS headerErrors=$TRANSPORT_HEADER_FAILURES)"
fi
log ""

PHASE26_SETTINGS_FAILURES=0
wait_phase26_apply() {
  local url="$1" request_id="$2" i state applied failed
  for i in $(seq 1 60); do
    state="$(curl -sS --http1.1 --max-time 3 "${url}?_=$(date +%s%N)" 2>>"$LOGFILE" || true)"
    applied="$(json_uint_field "$state" lastAppliedId)"
    failed="$(json_uint_field "$state" lastFailedId)"
    [[ -n "$failed" && "$failed" == "$request_id" ]] && return 2
    [[ -n "$applied" && "$applied" == "$request_id" ]] && return 0
    sleep 0.05
  done
  return 1
}

log "===== PHASE 0Q: PHASE-26 SPLIT BETRIEBSEINSTELLUNGEN SAVE ====="
compat_body="${OUTDIR}/phase26-legacy-compat.json"
compat_meta="$(post_json_capture "phase26" "async-saverunsettings-retired" "$BASE8080/saverunsettings" '{}' "$compat_body" 5)"; compat_rc=$?
IFS=$'\t' read -r compat_http compat_seconds compat_bytes <<< "$compat_meta"
if [[ "$compat_rc" -eq 0 && "$compat_http" == "410" ]]; then
  log "PASS permanent-port broad /saverunsettings compatibility route is retired with HTTP 410"
else
  log "FAIL permanent-port broad /saverunsettings retirement: HTTP=${compat_http:-000} rc=$compat_rc"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi

run_profile="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/settings/run-profile" 2>>"$LOGFILE" || true)"
rp_start="$(json_nested_string_field "$run_profile" stored startDate)"
rp_flower="$(json_nested_string_field "$run_profile" stored floweringStart)"
rp_dry="$(json_nested_string_field "$run_profile" stored dryingStart)"
rp_phase="$(json_nested_number_field "$run_profile" stored currentPhase)"
rp_mon="$(json_nested_bool_field "$run_profile" stored minVpdMonitoring)"
rp_min="$(json_nested_number_field "$run_profile" stored minVPD)"
rp_hyst="$(json_nested_number_field "$run_profile" stored vpdHysteresis)"
log "run-profile stored snapshot: start='${rp_start}' flower='${rp_flower}' dry='${rp_dry}' phase='${rp_phase}' minVPD='${rp_min}' hysteresis='${rp_hyst}' monitoring='${rp_mon}'"
invalid_rp_body="${OUTDIR}/phase26-run-profile-invalid.json"
invalid_rp_meta="$(post_json_capture "negative-test" "async-run-profile-invalid" "$BASE8080/api/settings/run-profile" '{"startDate":"2026-10-29","floweringStart":"","dryingStart":"","currentPhase":1,"minVpdMonitoring":false,"minVPD":0.99,"vpdHysteresis":0.05}' "$invalid_rp_body" 5)"; invalid_rp_rc=$?
IFS=$'\t' read -r invalid_rp_http _ _ <<< "$invalid_rp_meta"
if [[ "$invalid_rp_rc" -eq 0 && "$invalid_rp_http" == "400" ]]; then
  log "PASS run-profile out-of-range input rejected"
else
  log "FAIL run-profile invalid input was not rejected: HTTP=${invalid_rp_http:-000}"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi
if [[ "$run_profile" == *'"startDate":"'* && "$run_profile" == *'"floweringStart":"'* && "$run_profile" == *'"dryingStart":"'* && -n "$rp_phase" && -n "$rp_mon" && -n "$rp_min" && -n "$rp_hyst" ]]; then
  rp_body="${OUTDIR}/phase26-run-profile-noop.json"
  rp_payload="{\"startDate\":\"$(json_escape_string "$rp_start")\",\"floweringStart\":\"$(json_escape_string "$rp_flower")\",\"dryingStart\":\"$(json_escape_string "$rp_dry")\",\"currentPhase\":$rp_phase,\"minVpdMonitoring\":$rp_mon,\"minVPD\":$rp_min,\"vpdHysteresis\":$rp_hyst}"
  rp_meta="$(post_json_capture "phase26" "async-run-profile-noop" "$BASE8080/api/settings/run-profile" "$rp_payload" "$rp_body" 5)"; rp_rc=$?
  IFS=$'\t' read -r rp_http _ _ <<< "$rp_meta"; rp_resp="$(cat "$rp_body" 2>/dev/null || true)"; rp_id="$(json_uint_field "$rp_resp" requestId)"
  if [[ "$rp_rc" -eq 0 && "$rp_http" == "202" && -n "$rp_id" ]] && wait_phase26_apply "$BASE8080/api/settings/run-profile" "$rp_id"; then
    log "PASS run-profile same-value persist/verify/main-loop handoff: requestId=$rp_id"
  else
    log "FAIL run-profile no-op handoff: HTTP=${rp_http:-000} requestId=${rp_id:-missing} response='${rp_resp}'"
    PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
  fi
else
  log "FAIL run-profile state could not be parsed safely: $run_profile"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi

irr="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/settings/irrigation" 2>>"$LOGFILE" || true)"
irr_busy="$(json_bool_field "$irr" runtimeBusy)"
irr_aow="$(json_nested_number_field "$irr" stored amountOfWater)"
irr_tpt="$(json_nested_number_field "$irr" stored timePerTask)"
irr_bt="$(json_nested_number_field "$irr" stored betweenTasks)"
irr_amt="$(json_nested_number_field "$irr" stored amount)"
irr_p1="$(json_nested_bool_field "$irr" stored pump1Enabled)"
irr_p2="$(json_nested_bool_field "$irr" stored pump2Enabled)"
irr_p3="$(json_nested_bool_field "$irr" stored pump3Enabled)"
irr_stop="$(json_nested_bool_field "$irr" stored stopOnFloodAlarm)"
irr_min="$(json_nested_number_field "$irr" stored tankMin)"
irr_max="$(json_nested_number_field "$irr" stored tankMax)"
invalid_irr_body="${OUTDIR}/phase26-irrigation-invalid.json"
invalid_irr_meta="$(post_json_capture "negative-test" "async-irrigation-config-invalid" "$BASE8080/api/settings/irrigation" '{"amountOfWater":1,"timePerTask":1,"betweenTasks":1,"amount":100,"pump1Enabled":true,"pump2Enabled":true,"pump3Enabled":true,"stopOnFloodAlarm":false,"tankMin":0,"tankMax":0}' "$invalid_irr_body" 5)"; invalid_irr_rc=$?
IFS=$'\t' read -r invalid_irr_http _ _ <<< "$invalid_irr_meta"
if [[ "$invalid_irr_rc" -eq 0 && "$invalid_irr_http" == "400" ]]; then
  log "PASS irrigation-config out-of-range input rejected"
else
  log "FAIL irrigation-config invalid input was not rejected: HTTP=${invalid_irr_http:-000}"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi
if [[ "$irr_busy" == "true" ]]; then
  log "SKIP irrigation same-value handoff: watering/pump runtime currently active; safety gate intentionally refuses config writes"
elif [[ -n "$irr_aow" && -n "$irr_tpt" && -n "$irr_bt" && -n "$irr_amt" && -n "$irr_p1" && -n "$irr_p2" && -n "$irr_p3" && -n "$irr_stop" && -n "$irr_min" && -n "$irr_max" ]]; then
  irr_body="${OUTDIR}/phase26-irrigation-noop.json"
  irr_payload="{\"amountOfWater\":$irr_aow,\"timePerTask\":$irr_tpt,\"betweenTasks\":$irr_bt,\"amount\":$irr_amt,\"pump1Enabled\":$irr_p1,\"pump2Enabled\":$irr_p2,\"pump3Enabled\":$irr_p3,\"stopOnFloodAlarm\":$irr_stop,\"tankMin\":$irr_min,\"tankMax\":$irr_max}"
  irr_meta="$(post_json_capture "phase26" "async-irrigation-config-noop" "$BASE8080/api/settings/irrigation" "$irr_payload" "$irr_body" 5)"; irr_rc=$?
  IFS=$'\t' read -r irr_http _ _ <<< "$irr_meta"; irr_resp="$(cat "$irr_body" 2>/dev/null || true)"; irr_id="$(json_uint_field "$irr_resp" requestId)"
  if [[ "$irr_rc" -eq 0 && "$irr_http" == "202" && -n "$irr_id" ]] && wait_phase26_apply "$BASE8080/api/settings/irrigation" "$irr_id"; then
    log "PASS irrigation-config same-value persist/verify/main-loop handoff: requestId=$irr_id"
  else
    log "FAIL irrigation-config no-op handoff: HTTP=${irr_http:-000} requestId=${irr_id:-missing}"
    PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
  fi
else
  log "FAIL irrigation state could not be parsed safely: $irr"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi

# Dashboard save continues with heating-output. Test the exact canonical payload shape
# that the browser must send: source 0/2 always uses relay=0; source 1 uses 1..4.
heat_state="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/settings/heating-output" 2>>"$LOGFILE" || true)"
heat_stored_source="$(json_nested_number_field "$heat_state" stored sourceType)"
heat_stored_relay="$(json_nested_number_field "$heat_state" stored relay)"
heat_runtime_source="$(json_nested_number_field "$heat_state" runtime sourceType)"
heat_runtime_relay="$(json_nested_number_field "$heat_state" runtime relay)"
if [[ -n "$heat_stored_source" && -n "$heat_stored_relay" && -n "$heat_runtime_source" && -n "$heat_runtime_relay" ]]; then
  canonical_heat_relay="$heat_stored_relay"
  if [[ "$heat_stored_source" != "1" ]]; then canonical_heat_relay=0; fi
  # First prove that the old browser bug (relay:null) remains rejected by firmware.
  heat_null_body="${OUTDIR}/phase26-heating-null-negative.json"
  heat_null_meta="$(post_json_capture "negative-test" "async-heating-output-null" "$BASE8080/api/settings/heating-output" "{\"sourceType\":$heat_stored_source,\"relay\":null}" "$heat_null_body" 5)"; heat_null_rc=$?
  IFS=$'\t' read -r heat_null_http _ _ <<< "$heat_null_meta"
  if [[ "$heat_null_rc" -eq 0 && "$heat_null_http" == "400" ]]; then
    log "PASS heating-output rejects legacy dashboard relay:null payload"
  else
    log "FAIL heating-output unexpectedly accepted relay:null: HTTP=${heat_null_http:-000}"
    PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
  fi

  heat_body="${OUTDIR}/phase26-heating-dashboard-noop.json"
  heat_payload="{\"sourceType\":$heat_stored_source,\"relay\":$canonical_heat_relay}"
  heat_meta="$(post_json_capture "phase26" "async-heating-output-dashboard-noop" "$BASE8080/api/settings/heating-output" "$heat_payload" "$heat_body" 5)"; heat_rc=$?
  IFS=$'\t' read -r heat_http _ _ <<< "$heat_meta"; heat_resp="$(cat "$heat_body" 2>/dev/null || true)"; heat_id="$(json_uint_field "$heat_resp" requestId)"
  heat_ok=0
  if [[ "$heat_rc" -eq 0 && "$heat_http" == "200" && "$heat_resp" == *'"noChange":true'* ]]; then
    heat_ok=1
  elif [[ "$heat_rc" -eq 0 && "$heat_http" == "202" && -n "$heat_id" ]] && wait_phase26_apply "$BASE8080/api/settings/heating-output" "$heat_id"; then
    heat_ok=1
  fi
  if [[ "$heat_ok" -eq 1 ]]; then
    log "PASS heating-output dashboard-compatible same-value payload: sourceType=$heat_stored_source relay=$canonical_heat_relay HTTP=$heat_http"
  else
    log "FAIL heating-output dashboard-compatible no-op: sourceType=$heat_stored_source relay=$canonical_heat_relay HTTP=${heat_http:-000} response='${heat_resp}'"
    PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
  fi
else
  log "FAIL heating-output state could not be parsed safely: $heat_state"
  PHASE26_SETTINGS_FAILURES=$((PHASE26_SETTINGS_FAILURES + 1))
fi

if [[ "$PHASE26_SETTINGS_FAILURES" -eq 0 ]]; then
  log "PHASE 26 SPLIT BETRIEBSEINSTELLUNGEN SAVE: PASS"
else
  log "PHASE 26 SPLIT BETRIEBSEINSTELLUNGEN SAVE: FAIL ($PHASE26_SETTINGS_FAILURES problem(s))"
fi
log ""
PHASE27_SHELLY_FAILURES=0
log "===== PHASE 0R: PHASE-27 SHELLY RUNTIME ACTION HANDOFF (DRY-RUN / NO NETWORK) ====="

wait_phase27_shelly_action() {
  local request_id="$1" status body state
  for _ in $(seq 1 120); do
    body="$(curl -sS --max-time 3 "$BASE8080/api/shelly/action-status?requestId=$request_id" 2>>"$LOGFILE" || true)"
    state="$(json_string_field "$body" state)"
    if [[ "$state" == "applied" ]]; then
      printf '%s' "$body"
      return 0
    fi
    if [[ "$state" == "failed" ]]; then
      printf '%s' "$body"
      return 1
    fi
    sleep 0.05
  done
  return 2
}

phase27_health_before="$(fetch_async_health 5 || true)"
phase27_net_before="$(json_uint_field "$phase27_health_before" shellyActionNetworkOps)"; phase27_net_before="${phase27_net_before:-0}"
phase27_applied_before="$(json_uint_field "$phase27_health_before" shellyActionApplied)"; phase27_applied_before="${phase27_applied_before:-0}"
phase27_dry_before="$(json_uint_field "$phase27_health_before" shellyActionDryRuns)"; phase27_dry_before="${phase27_dry_before:-0}"

phase27_js="$(curl -sS --max-time 8 "$BASE8080/script.js" 2>>"$LOGFILE" || true)"
if grep -q 'waitShellyActionApplied' <<<"$phase27_js" && grep -q "device === 'mainSw' ? 'main'" <<<"$phase27_js" && grep -q '/api/shelly/action-status?requestId=' <<<"$phase27_js"; then
  log "PASS dashboard Shelly actions wait for main-loop completion and mainSw maps to main"
else
  log "FAIL dashboard Shelly action queue/status integration missing"
  PHASE27_SHELLY_FAILURES=$((PHASE27_SHELLY_FAILURES + 1))
fi

phase27_paths=(
  "/shelly/main/toggle"
  "/shelly/light/toggle"
  "/shelly/humidifier/toggle"
  "/shelly/heater/toggle"
  "/shelly/fan/toggle"
  "/shelly/exhaust/toggle"
  "/api/shelly/light/refresh-schedule"
  "/api/shelly/humidifier/refresh-schedule"
  "/api/shelly/heater/refresh-schedule"
  "/api/shelly/fan/refresh-schedule"
  "/api/shelly/exhaust/refresh-schedule"
  "/api/shelly/reset-energy"
)

phase27_index=0
for path in "${phase27_paths[@]}"; do
  phase27_index=$((phase27_index + 1))
  bodyfile="${OUTDIR}/phase27-shelly-${phase27_index}.json"
  meta="$(post_empty_capture "phase27" "async-shelly-dryrun-${phase27_index}" "$BASE8080$path?dryRun=1" "$bodyfile" 5)"; rc=$?
  IFS=$'\t' read -r http seconds bytes <<< "$meta"
  body="$(cat "$bodyfile" 2>/dev/null || true)"
  req_id="$(json_uint_field "$body" requestId)"
  if [[ "$rc" -ne 0 || "$http" != "202" || -z "$req_id" || "$(json_bool_field "$body" dryRun)" != "true" ]]; then
    log "FAIL Shelly dry-run queue path=$path HTTP=$http rc=$rc response=$body"
    PHASE27_SHELLY_FAILURES=$((PHASE27_SHELLY_FAILURES + 1))
    continue
  fi
  status="$(wait_phase27_shelly_action "$req_id")"; wait_rc=$?
  if [[ "$wait_rc" -eq 0 && "$(json_string_field "$status" state)" == "applied" && "$status" == *'"networkAccess":false'* ]]; then
    log "PASS Shelly dry-run main-loop handoff path=$path requestId=$req_id"
else
    log "FAIL Shelly dry-run apply path=$path requestId=$req_id waitRc=$wait_rc status=$status"
    PHASE27_SHELLY_FAILURES=$((PHASE27_SHELLY_FAILURES + 1))
  fi
done

phase27_health_after="$(fetch_async_health 5 || true)"
phase27_net_after="$(json_uint_field "$phase27_health_after" shellyActionNetworkOps)"; phase27_net_after="${phase27_net_after:-0}"
phase27_applied_after="$(json_uint_field "$phase27_health_after" shellyActionApplied)"; phase27_applied_after="${phase27_applied_after:-0}"
phase27_dry_after="$(json_uint_field "$phase27_health_after" shellyActionDryRuns)"; phase27_dry_after="${phase27_dry_after:-0}"
expected_phase27_actions="${#phase27_paths[@]}"
if [[ "$phase27_net_after" -eq "$phase27_net_before" && "$phase27_applied_after" -eq $((phase27_applied_before + expected_phase27_actions)) && "$phase27_dry_after" -eq $((phase27_dry_before + expected_phase27_actions)) ]]; then
  log "PASS Phase 27 counters: applied +$expected_phase27_actions dryRuns +$expected_phase27_actions networkOps unchanged ($phase27_net_before)"
else
  log "FAIL Phase 27 counters: applied $phase27_applied_before->$phase27_applied_after dryRuns $phase27_dry_before->$phase27_dry_after networkOps $phase27_net_before->$phase27_net_after"
  PHASE27_SHELLY_FAILURES=$((PHASE27_SHELLY_FAILURES + 1))
fi

if [[ "$PHASE27_SHELLY_FAILURES" -eq 0 ]]; then
  log "PHASE 27 SHELLY RUNTIME ACTION HANDOFF: PASS"
else
  log "PHASE 27 SHELLY RUNTIME ACTION HANDOFF: FAIL ($PHASE27_SHELLY_FAILURES problem(s))"
fi
log ""

PHASE28_SHELLY_SETTINGS_FAILURES=0
log "===== PHASE 0S: PHASE-28 SHELLY DASHBOARD SETTINGS SAVE (SAME-VALUE / TEST DETECTION DISABLED) ====="
phase28_js="$(curl -sS --max-time 8 "$BASE8080/script.js" 2>>"$LOGFILE" || true)"
phase28_root="$(curl -sS --max-time 8 "$BASE8080/" 2>>"$LOGFILE" || true)"
if grep -q 'id="shellySettingsForm"' <<<"$phase28_root" && \
   grep -q 'onsubmit="event.preventDefault(); window.saveShellySettingsAll(event); return false;"' <<<"$phase28_root" && \
   grep -q 'id="saveshellysettingsBtn"' <<<"$phase28_root" && \
   grep -q 'onclick="window.saveShellySettingsAll(event)"' <<<"$phase28_root" && \
   grep -q "saveShellySettingsAll" <<<"$phase28_js" && \
   grep -q "/api/settings/shelly-dashboard" <<<"$phase28_js" && \
   grep -q "lightScheduleDirty" <<<"$phase28_js" && \
   grep -q "payload.programLightSchedule = true" <<<"$phase28_js" && \
   grep -q "waitShellyActionApplied" <<<"$phase28_js"; then
  log "PASS dashboard Shelly form is directly bound to permanent-port settings + main-loop handoff"
else
  log "FAIL dashboard Shelly direct settings binding missing"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
fi

if ! grep -q 'id="shellyExhaustGen"' <<<"$phase28_root" && \
   grep -q 'id="shellyExhaustGenDetected"' <<<"$phase28_root" && \
   grep -q 'id="shellyMainSwGenDetected"' <<<"$phase28_root" && \
   grep -q 'detectGenerations: true' <<<"$phase28_js" && \
   grep -q 'refreshShellyDetectedGenerations' <<<"$phase28_js" && \
   ! grep -q "gen('shellyMainSwGen')" <<<"$phase28_js"; then
  log "PASS dashboard removes generation selectors and enables automatic generation detection"
else
  log "FAIL dashboard automatic generation detection UI/binding missing"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
fi

if ! grep -q 'shelly.generationDetected' <<<"$phase28_root" && \
   ! grep -q 'Erkannt:' <<<"$phase28_root" && \
   grep -q 'class="shelly-gen-detected" id="shellyExhaustGenDetected"' <<<"$phase28_root"; then
  log "PASS detected Shelly generation is shown as plain GenX text without prefix"
else
  log "FAIL detected Shelly generation still has prefix or old wrapper"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
fi

if grep -q "function setSwitchWithMetrics(baseId, status, power, totalWh, costEur, generation)" <<<"$phase28_js" && \
   grep -q "stateEl.style.display = 'flex'" <<<"$phase28_js" && \
   grep -q "stateEl.innerHTML = hideMetrics ? ''" <<<"$phase28_js" && \
   grep -q "data\['settings.shelly.exhaust.gen'\]" <<<"$phase28_js"; then
  log "PASS Gen1 dashboard keeps ON/OFF strip visible and suppresses unsupported Watt/Wh text"
else
  log "FAIL Gen1 dashboard status-strip/metric suppression behavior missing"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
fi

if grep -q 'action="/saveshellysettings"' <<<"$phase28_root"; then
  log "FAIL dashboard still contains legacy action=/saveshellysettings fallback"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
else
  log "PASS dashboard has no legacy /saveshellysettings form action fallback"
fi

legacy_shelly_body="${OUTDIR}/phase28-legacy-saveshellysettings.json"
legacy_shelly_meta="$(post_empty_capture "phase28" "async-saveshellysettings-retired" "$BASE8080/saveshellysettings" "$legacy_shelly_body" 5)"; legacy_shelly_rc=$?
IFS=$'\t' read -r legacy_shelly_http _ _ <<< "$legacy_shelly_meta"
legacy_shelly_resp="$(cat "$legacy_shelly_body" 2>/dev/null || true)"
if [[ "$legacy_shelly_rc" -eq 0 && "$legacy_shelly_http" == "410" && "$legacy_shelly_resp" == *'legacy_shellysettings_retired'* ]]; then
  log "PASS legacy /saveshellysettings retired on permanent port with HTTP 410"
else
  log "FAIL legacy /saveshellysettings retirement: HTTP=${legacy_shelly_http:-000} rc=$legacy_shelly_rc response='$legacy_shelly_resp'"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
fi

phase28_state="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly-dashboard" 2>>"$LOGFILE" || true)"
phase28_ok="$(json_bool_field "$phase28_state" ok)"
if [[ "$phase28_ok" != "true" ]]; then
  log "FAIL Shelly dashboard settings pre-read: $phase28_state"
  PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
else
  phase28_payload='{"devices":{'
  for idx in "${!SHELLY_ROLES[@]}"; do
    role="${SHELLY_ROLES[$idx]}"
    ip="$(json_nested2_string_field "$phase28_state" devices "$role" ip)"
    [[ "$idx" -eq 0 ]] || phase28_payload+=','
    phase28_payload+="\"$role\":{\"ip\":\"$(json_escape_string "$ip")\"}"
  done
  phase28_user="$(json_string_field "$phase28_state" username)"
  phase28_flood_enabled="$(json_bool_field "$phase28_state" floodEnabled)"
  phase28_flood_token="$(json_string_field "$phase28_state" floodToken)"
  phase28_light_on="$(json_string_field "$phase28_state" lightOnTime)"
  phase28_light_hours="$(json_uint_field "$phase28_state" lightDayHours)"
  phase28_payload+='}'
  phase28_payload+=",\"username\":\"$(json_escape_string "$phase28_user")\""
  phase28_payload+=",\"floodEnabled\":${phase28_flood_enabled:-false}"
  phase28_payload+=",\"floodToken\":\"$(json_escape_string "$phase28_flood_token")\""
  phase28_payload+=",\"lightOnTime\":\"$(json_escape_string "$phase28_light_on")\""
  phase28_payload+=",\"lightDayHours\":${phase28_light_hours:-18},\"detectGenerations\":false,\"programLightSchedule\":false}"

  phase28_health_before="$(fetch_async_health 5 || true)"
  phase28_cfg_req_before="$(json_uint_field "$phase28_health_before" shellyConfigWriteRequests)"; phase28_cfg_req_before="${phase28_cfg_req_before:-0}"
  phase28_cfg_fail_before="$(json_uint_field "$phase28_health_before" shellyConfigWriteFailures)"; phase28_cfg_fail_before="${phase28_cfg_fail_before:-0}"
  phase28_net_before="$(json_uint_field "$phase28_health_before" shellyActionNetworkOps)"; phase28_net_before="${phase28_net_before:-0}"
  phase28_applied_before="$(json_uint_field "$phase28_health_before" shellyActionApplied)"; phase28_applied_before="${phase28_applied_before:-0}"

  # Regression for the real browser case: a user may only define a Shelly and
  # must not be forced to submit or validate a light schedule.
  phase28_payload_no_light='{"devices":{'
  for idx in "${!SHELLY_ROLES[@]}"; do
    role="${SHELLY_ROLES[$idx]}"
    ip="$(json_nested2_string_field "$phase28_state" devices "$role" ip)"
    [[ "$idx" -eq 0 ]] || phase28_payload_no_light+=','
    phase28_payload_no_light+="\"$role\":{\"ip\":\"$(json_escape_string "$ip")\"}"
  done
  phase28_payload_no_light+='}'
  phase28_payload_no_light+=",\"username\":\"$(json_escape_string "$phase28_user")\""
  phase28_payload_no_light+=",\"floodEnabled\":${phase28_flood_enabled:-false}"
  phase28_payload_no_light+=",\"floodToken\":\"$(json_escape_string "$phase28_flood_token")\""
  phase28_payload_no_light+=",\"detectGenerations\":false,\"programLightSchedule\":false}"

  phase28_no_light_body="${OUTDIR}/phase28-shelly-dashboard-no-light.json"
  phase28_no_light_meta="$(post_json_capture "phase28" "async-shelly-dashboard-no-light" "$BASE8080/api/settings/shelly-dashboard" "$phase28_payload_no_light" "$phase28_no_light_body" 5)"; phase28_no_light_rc=$?
  IFS=$'\t' read -r phase28_no_light_http _ _ <<< "$phase28_no_light_meta"
  phase28_no_light_resp="$(cat "$phase28_no_light_body" 2>/dev/null || true)"
  phase28_no_light_id="$(json_uint_field "$phase28_no_light_resp" requestId)"
  if [[ "$phase28_no_light_rc" -eq 0 && "$phase28_no_light_http" == "202" && -n "$phase28_no_light_id" ]]; then
    phase28_no_light_status="$(wait_phase27_shelly_action "$phase28_no_light_id")"; phase28_no_light_wait_rc=$?
    if [[ "$phase28_no_light_wait_rc" -eq 0 && "$(json_string_field "$phase28_no_light_status" state)" == "applied" && "$phase28_no_light_status" == *'"networkAccess":false'* ]]; then
      phase28_no_light_state="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly-dashboard" 2>>"$LOGFILE" || true)"
      if [[ "$(json_string_field "$phase28_no_light_state" lightOnTime)" == "$phase28_light_on" && \
            "$(json_uint_field "$phase28_no_light_state" lightDayHours)" == "$phase28_light_hours" ]]; then
        log "PASS Shelly-only dashboard save works without light schedule and preserves stored light timing requestId=$phase28_no_light_id"
      else
        log "FAIL Shelly-only save changed light schedule unexpectedly: before=$phase28_light_on/$phase28_light_hours after=$(json_string_field "$phase28_no_light_state" lightOnTime)/$(json_uint_field "$phase28_no_light_state" lightDayHours)"
        PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
      fi
    else
      log "FAIL Shelly-only save without light schedule did not apply: requestId=$phase28_no_light_id waitRc=$phase28_no_light_wait_rc status=$phase28_no_light_status"
      PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
    fi
  else
    log "FAIL Shelly-only save without light schedule: HTTP=${phase28_no_light_http:-000} rc=$phase28_no_light_rc response='$phase28_no_light_resp'"
    PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
  fi

  phase28_body="${OUTDIR}/phase28-shelly-dashboard-noop.json"
  phase28_meta="$(post_json_capture "phase28" "async-shelly-dashboard-noop" "$BASE8080/api/settings/shelly-dashboard" "$phase28_payload" "$phase28_body" 5)"; phase28_rc=$?
  IFS=$'\t' read -r phase28_http _ _ <<< "$phase28_meta"
  phase28_resp="$(cat "$phase28_body" 2>/dev/null || true)"
  phase28_id="$(json_uint_field "$phase28_resp" requestId)"
  if [[ "$phase28_rc" -eq 0 && "$phase28_http" == "202" && -n "$phase28_id" ]]; then
    phase28_status="$(wait_phase27_shelly_action "$phase28_id")"; phase28_wait_rc=$?
    if [[ "$phase28_wait_rc" -eq 0 && "$(json_string_field "$phase28_status" state)" == "applied" && "$phase28_status" == *'"networkAccess":false'* ]]; then
      log "PASS Shelly dashboard same-value persist/verify/main-loop reload requestId=$phase28_id"
    else
      log "FAIL Shelly dashboard main-loop reload requestId=$phase28_id waitRc=$phase28_wait_rc status=$phase28_status"
      PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
    fi
  else
    log "FAIL Shelly dashboard save: HTTP=${phase28_http:-000} rc=$phase28_rc response='$phase28_resp'"
    PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
  fi

  phase28_state_after="$(curl -sS --max-time 5 "$BASE8080/api/settings/shelly-dashboard" 2>>"$LOGFILE" || true)"
  if [[ "$phase28_state_after" == "$phase28_state" ]]; then
    log "PASS Shelly dashboard same-value save preserved stored configuration byte-for-byte"
  else
    # JSON field order is stable, but generated Flood tokens can legitimately be
    # filled if an enabled legacy configuration had none. Compare core values too.
    phase28_core_ok=1
    for idx in "${!SHELLY_ROLES[@]}"; do
      role="${SHELLY_ROLES[$idx]}"
      [[ "$(json_nested2_string_field "$phase28_state_after" devices "$role" ip)" == "$(json_nested2_string_field "$phase28_state" devices "$role" ip)" ]] || phase28_core_ok=0
      [[ "$(json_nested2_number_field "$phase28_state_after" devices "$role" gen)" == "$(json_nested2_number_field "$phase28_state" devices "$role" gen)" ]] || phase28_core_ok=0
    done
    [[ "$(json_string_field "$phase28_state_after" username)" == "$phase28_user" ]] || phase28_core_ok=0
    [[ "$(json_bool_field "$phase28_state_after" floodEnabled)" == "$phase28_flood_enabled" ]] || phase28_core_ok=0
    [[ "$(json_string_field "$phase28_state_after" lightOnTime)" == "$phase28_light_on" ]] || phase28_core_ok=0
    [[ "$(json_uint_field "$phase28_state_after" lightDayHours)" == "$phase28_light_hours" ]] || phase28_core_ok=0
    if [[ "$phase28_core_ok" -eq 1 ]]; then
      log "PASS Shelly dashboard same-value save preserved all core stored settings"
    else
      log "FAIL Shelly dashboard stored readback changed unexpectedly: before=$phase28_state after=$phase28_state_after"
      PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
    fi
  fi

  phase28_health_after="$(fetch_async_health 5 || true)"
  phase28_cfg_req_after="$(json_uint_field "$phase28_health_after" shellyConfigWriteRequests)"; phase28_cfg_req_after="${phase28_cfg_req_after:-0}"
  phase28_cfg_fail_after="$(json_uint_field "$phase28_health_after" shellyConfigWriteFailures)"; phase28_cfg_fail_after="${phase28_cfg_fail_after:-0}"
  phase28_net_after="$(json_uint_field "$phase28_health_after" shellyActionNetworkOps)"; phase28_net_after="${phase28_net_after:-0}"
  phase28_applied_after="$(json_uint_field "$phase28_health_after" shellyActionApplied)"; phase28_applied_after="${phase28_applied_after:-0}"
  if [[ "$phase28_cfg_req_after" -eq $((phase28_cfg_req_before + 2)) && "$phase28_cfg_fail_after" -eq "$phase28_cfg_fail_before" && "$phase28_net_after" -eq "$phase28_net_before" && "$phase28_applied_after" -eq $((phase28_applied_before + 2)) ]]; then
    log "PASS Phase 28 counters: configWrites +2 apply +2 networkOps unchanged ($phase28_net_before)"
  else
    log "FAIL Phase 28 counters: configWrites $phase28_cfg_req_before->$phase28_cfg_req_after failures $phase28_cfg_fail_before->$phase28_cfg_fail_after applied $phase28_applied_before->$phase28_applied_after networkOps $phase28_net_before->$phase28_net_after"
    PHASE28_SHELLY_SETTINGS_FAILURES=$((PHASE28_SHELLY_SETTINGS_FAILURES + 1))
  fi
fi

if [[ "$PHASE28_SHELLY_SETTINGS_FAILURES" -eq 0 ]]; then
  log "PHASE 28 SHELLY DASHBOARD SETTINGS SAVE: PASS"
else
  log "PHASE 28 SHELLY DASHBOARD SETTINGS SAVE: FAIL ($PHASE28_SHELLY_SETTINGS_FAILURES problem(s))"
fi
log ""


log ""
log "===== PHASE 0T: PHASE-29 SYSTEM SETTINGS DASHBOARD + MAIN-LOOP RESTART HANDOFF ====="
PHASE29_SYSTEM_SETTINGS_FAILURES=0
phase29_js="$(curl -sS --max-time 8 "$BASE8080/script.js" 2>>"$LOGFILE" || true)"
phase29_root="$(curl -sS --max-time 8 "$BASE8080/" 2>>"$LOGFILE" || true)"

if grep -q 'id="systemSettingsForm"' <<<"$phase29_root" && \
   grep -q 'onsubmit="event.preventDefault(); window.saveSystemSettingsAll(event); return false;"' <<<"$phase29_root" && \
   grep -q 'id="saveSettingsBtn"' <<<"$phase29_root" && \
   grep -q 'onclick="window.saveSystemSettingsAll(event)"' <<<"$phase29_root" && \
   grep -q 'window.saveSystemSettingsAll' <<<"$phase29_js" && \
   grep -q '/api/settings/active-relay-count' <<<"$phase29_js" && \
   grep -q '/api/settings/time-zone' <<<"$phase29_js" && \
   grep -q '/api/settings/ds18b20-enabled' <<<"$phase29_js" && \
   grep -q '/api/settings/relay-name/4' <<<"$phase29_js" && \
   grep -q '/api/system/restart' <<<"$phase29_js"; then
  log "PASS dashboard System Settings form uses split permanent-port APIs + restart handoff"
else
  log "FAIL dashboard System Settings form is not fully bound to Phase-29 permanent-port save/restart flow"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
fi

if grep -q 'action="/savesettings"' <<<"$phase29_root"; then
  log "FAIL dashboard still contains legacy /savesettings form action fallback"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
else
  log "PASS dashboard has no legacy /savesettings form action fallback"
fi

phase29_legacy_body="${OUTDIR}/phase29-legacy-savesettings.json"
phase29_legacy_meta="$(post_empty_capture "phase29" "async-savesettings-retired" "$BASE8080/savesettings" "$phase29_legacy_body" 5)"; phase29_legacy_rc=$?
IFS=$'\t' read -r phase29_legacy_http _ _ <<< "$phase29_legacy_meta"
if [[ "$phase29_legacy_rc" -eq 0 && "$phase29_legacy_http" == "410" ]]; then
  log "PASS legacy /savesettings retired on permanent port with HTTP 410"
else
  log "FAIL legacy /savesettings retirement: HTTP=${phase29_legacy_http:-000} rc=$phase29_legacy_rc body='$(cat "$phase29_legacy_body" 2>/dev/null || true)'"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
fi

phase29_health_before="$(fetch_async_health 5 || true)"
phase29_req_before="$(json_uint_field "$phase29_health_before" systemRestartRequests)"; phase29_req_before="${phase29_req_before:-0}"
phase29_fail_before="$(json_uint_field "$phase29_health_before" systemRestartFailures)"; phase29_fail_before="${phase29_fail_before:-0}"
phase29_queued_before="$(json_uint_field "$phase29_health_before" systemRestartQueued)"; phase29_queued_before="${phase29_queued_before:-0}"
phase29_applied_before="$(json_uint_field "$phase29_health_before" systemRestartApplied)"; phase29_applied_before="${phase29_applied_before:-0}"
phase29_dry_before="$(json_uint_field "$phase29_health_before" systemRestartDryRuns)"; phase29_dry_before="${phase29_dry_before:-0}"
phase29_uptime_before="$(json_uint_field "$phase29_health_before" uptimeMs)"; phase29_uptime_before="${phase29_uptime_before:-0}"

phase29_restart_body="${OUTDIR}/phase29-restart-dryrun.json"
phase29_restart_meta="$(post_empty_capture "phase29" "async-system-restart-dryrun" "$BASE8080/api/system/restart?dryRun=1" "$phase29_restart_body" 5)"; phase29_restart_rc=$?
IFS=$'\t' read -r phase29_restart_http _ _ <<< "$phase29_restart_meta"
phase29_restart_resp="$(cat "$phase29_restart_body" 2>/dev/null || true)"
phase29_restart_id="$(json_uint_field "$phase29_restart_resp" requestId)"
if [[ "$phase29_restart_rc" -eq 0 && "$phase29_restart_http" == "202" && -n "$phase29_restart_id" ]]; then
  phase29_status=""
  for _ in $(seq 1 30); do
    phase29_status="$(curl -sS --max-time 2 "$BASE8080/api/system/restart-status?requestId=${phase29_restart_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase29_status" state)" == "applied" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase29_status" state)" == "applied" && "$(json_bool_field "$phase29_status" dryRun)" == "true" ]]; then
    log "PASS system restart dry-run queued and applied in main loop requestId=$phase29_restart_id"
  else
    log "FAIL system restart dry-run did not reach applied state: requestId=$phase29_restart_id status='$phase29_status'"
    PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
  fi
else
  log "FAIL system restart dry-run queue: HTTP=${phase29_restart_http:-000} rc=$phase29_restart_rc response='$phase29_restart_resp'"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
fi

sleep 1.8
phase29_health_after="$(fetch_async_health 5 || true)"
phase29_req_after="$(json_uint_field "$phase29_health_after" systemRestartRequests)"; phase29_req_after="${phase29_req_after:-0}"
phase29_fail_after="$(json_uint_field "$phase29_health_after" systemRestartFailures)"; phase29_fail_after="${phase29_fail_after:-0}"
phase29_queued_after="$(json_uint_field "$phase29_health_after" systemRestartQueued)"; phase29_queued_after="${phase29_queued_after:-0}"
phase29_applied_after="$(json_uint_field "$phase29_health_after" systemRestartApplied)"; phase29_applied_after="${phase29_applied_after:-0}"
phase29_dry_after="$(json_uint_field "$phase29_health_after" systemRestartDryRuns)"; phase29_dry_after="${phase29_dry_after:-0}"
phase29_uptime_after="$(json_uint_field "$phase29_health_after" uptimeMs)"; phase29_uptime_after="${phase29_uptime_after:-0}"

if [[ "$phase29_req_after" -eq $((phase29_req_before + 1)) && \
      "$phase29_fail_after" -eq "$phase29_fail_before" && \
      "$phase29_queued_after" -eq $((phase29_queued_before + 1)) && \
      "$phase29_applied_after" -eq $((phase29_applied_before + 1)) && \
      "$phase29_dry_after" -eq $((phase29_dry_before + 1)) ]]; then
  log "PASS Phase 29 restart counters: requests/queued/applied/dryRuns +1, failures unchanged"
else
  log "FAIL Phase 29 restart counters: req $phase29_req_before->$phase29_req_after fail $phase29_fail_before->$phase29_fail_after queued $phase29_queued_before->$phase29_queued_after applied $phase29_applied_before->$phase29_applied_after dry $phase29_dry_before->$phase29_dry_after"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
fi

if [[ "$phase29_uptime_after" -gt "$phase29_uptime_before" && "$(json_string_field "$phase29_health_after" version)" == "$EXPECTED_FW_VERSION" ]]; then
  log "PASS Phase 29 dry-run did not reboot firmware; uptime continued $phase29_uptime_before->$phase29_uptime_after"
else
  log "FAIL Phase 29 dry-run reboot guard: uptime $phase29_uptime_before->$phase29_uptime_after health='$phase29_health_after'"
  PHASE29_SYSTEM_SETTINGS_FAILURES=$((PHASE29_SYSTEM_SETTINGS_FAILURES + 1))
fi

if [[ "$PHASE29_SYSTEM_SETTINGS_FAILURES" -eq 0 ]]; then
  log "PHASE 29 SYSTEM SETTINGS DASHBOARD + RESTART HANDOFF: PASS"
else
  log "PHASE 29 SYSTEM SETTINGS DASHBOARD + RESTART HANDOFF: FAIL ($PHASE29_SYSTEM_SETTINGS_FAILURES problem(s))"
fi
log ""

log "===== PHASE 0U: PHASE-30 FACTORY RESET HANDOFF + AP WIFI PORT-80 EXCEPTION ====="
PHASE30_FACTORY_RESET_FAILURES=0

# Phase 32 removed the normal-STA TCP/80 compatibility server completely.
# Phase 30's AP ownership invariant is therefore stronger now: /save and
# /factory-reset must be unreachable on TCP/80 while STA mode is active.
if probe_port80_closed "phase30-ap-save" "/save" "POST"; then
  log "PASS normal-STA TCP/80 is closed; Wi-Fi provisioning /save exists only when the Async AP portal is active"
else
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

phase30_ap8080_body="${OUTDIR}/phase30-ap-save-port8080.body"
phase30_ap8080_meta="$(post_empty_capture "phase30" "ap-wifi-save-port8080-guard" "$BASE8080/save" "$phase30_ap8080_body" 5)"; phase30_ap8080_rc=$?
IFS=$'\t' read -r phase30_ap8080_http _ _ <<< "$phase30_ap8080_meta"
if [[ "$phase30_ap8080_rc" -eq 0 && "$phase30_ap8080_http" == "410" && "$(cat "$phase30_ap8080_body" 2>/dev/null || true)" == *'"ap_wifi_save_port80_only"'* ]]; then
  log "PASS permanent port explicitly rejects AP Wi-Fi /save; provisioning stays TCP/80-only"
else
  log "FAIL permanent-port /save guard: HTTP=${phase30_ap8080_http:-000} rc=$phase30_ap8080_rc body='$(cat "$phase30_ap8080_body" 2>/dev/null || true)'"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

if probe_port80_closed "phase30-factory-reset" "/factory-reset" "POST"; then
  log "PASS factory reset has no TCP/80 STA route after Phase 32 WebServer removal"
else
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

# Missing confirmation must be rejected before queueing/destructive work.
phase30_bad_body="${OUTDIR}/phase30-factory-reset-no-confirm.json"
phase30_bad_meta="$(post_empty_capture "phase30" "async-factory-reset-no-confirm" "$BASE8080/factory-reset?dryRun=1" "$phase30_bad_body" 5)"; phase30_bad_rc=$?
IFS=$'\t' read -r phase30_bad_http _ _ <<< "$phase30_bad_meta"
if [[ "$phase30_bad_rc" -eq 0 && "$phase30_bad_http" == "400" && "$(cat "$phase30_bad_body" 2>/dev/null || true)" == *'"factory_reset_confirmation_required"'* ]]; then
  log "PASS factory reset requires explicit confirm=1 before queueing"
else
  log "FAIL factory reset confirmation guard: HTTP=${phase30_bad_http:-000} rc=$phase30_bad_rc body='$(cat "$phase30_bad_body" 2>/dev/null || true)'"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

phase30_health_before="$(fetch_async_health 5 || true)"
phase30_req_before="$(json_uint_field "$phase30_health_before" factoryResetRequests)"; phase30_req_before="${phase30_req_before:-0}"
phase30_fail_before="$(json_uint_field "$phase30_health_before" factoryResetFailures)"; phase30_fail_before="${phase30_fail_before:-0}"
phase30_queued_before="$(json_uint_field "$phase30_health_before" factoryResetQueued)"; phase30_queued_before="${phase30_queued_before:-0}"
phase30_applied_before="$(json_uint_field "$phase30_health_before" factoryResetApplied)"; phase30_applied_before="${phase30_applied_before:-0}"
phase30_apply_fail_before="$(json_uint_field "$phase30_health_before" factoryResetApplyFailures)"; phase30_apply_fail_before="${phase30_apply_fail_before:-0}"
phase30_dry_before="$(json_uint_field "$phase30_health_before" factoryResetDryRuns)"; phase30_dry_before="${phase30_dry_before:-0}"
phase30_uptime_before="$(json_uint_field "$phase30_health_before" uptimeMs)"; phase30_uptime_before="${phase30_uptime_before:-0}"
phase30_ap_port="$(json_uint_field "$phase30_health_before" apWifiSavePort)"
phase30_ap_perm="$(json_bool_field "$phase30_health_before" apWifiSaveOnPermanentPort)"
if [[ "$phase30_ap_port" == "80" && "$phase30_ap_perm" == "false" ]]; then
  log "PASS firmware health declares AP Wi-Fi provisioning exception: port=80 permanentPort=false"
else
  log "FAIL AP Wi-Fi provisioning health contract: port=${phase30_ap_port:-missing} permanentPort=${phase30_ap_perm:-missing}"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

phase30_box_before="$(curl -sS --max-time 5 "$BASE8080/api/settings/box-name" 2>>"$LOGFILE" || true)"
phase30_box_stored_before="$(json_string_field "$phase30_box_before" stored)"

# Safe dry-run exercises the exact queue/main-loop handoff without Preferences.clear()
# and without restart.
phase30_reset_body="${OUTDIR}/phase30-factory-reset-dryrun.json"
phase30_reset_meta="$(curl -sS --http1.1 --max-time 5 -o "$phase30_reset_body" \
  -H 'Content-Type: application/x-www-form-urlencoded' -X POST --data 'confirm=1' \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/factory-reset?dryRun=1" 2>>"$LOGFILE")"; phase30_reset_rc=$?
record_result "phase30" "async-factory-reset-dryrun" "$phase30_reset_meta" "$phase30_reset_rc"
IFS=$'\t' read -r phase30_reset_http _ _ <<< "$phase30_reset_meta"
phase30_reset_resp="$(cat "$phase30_reset_body" 2>/dev/null || true)"
phase30_reset_id="$(json_uint_field "$phase30_reset_resp" requestId)"
if [[ "$phase30_reset_rc" -eq 0 && "$phase30_reset_http" == "202" && -n "$phase30_reset_id" && "$phase30_reset_resp" == *'"dryRun":true'* ]]; then
  phase30_status=""
  for _ in $(seq 1 30); do
    phase30_status="$(curl -sS --max-time 2 "$BASE8080/api/system/factory-reset-status?requestId=${phase30_reset_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase30_status" state)" == "applied" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase30_status" state)" == "applied" && "$(json_bool_field "$phase30_status" dryRun)" == "true" ]]; then
    log "PASS factory reset dry-run queued and applied in main loop requestId=$phase30_reset_id"
  else
    log "FAIL factory reset dry-run did not reach applied state: requestId=$phase30_reset_id status='$phase30_status'"
    PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
  fi
else
  log "FAIL factory reset dry-run queue: HTTP=${phase30_reset_http:-000} rc=$phase30_reset_rc response='$phase30_reset_resp'"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

sleep 0.5
phase30_health_after="$(fetch_async_health 5 || true)"
phase30_req_after="$(json_uint_field "$phase30_health_after" factoryResetRequests)"; phase30_req_after="${phase30_req_after:-0}"
phase30_fail_after="$(json_uint_field "$phase30_health_after" factoryResetFailures)"; phase30_fail_after="${phase30_fail_after:-0}"
phase30_queued_after="$(json_uint_field "$phase30_health_after" factoryResetQueued)"; phase30_queued_after="${phase30_queued_after:-0}"
phase30_applied_after="$(json_uint_field "$phase30_health_after" factoryResetApplied)"; phase30_applied_after="${phase30_applied_after:-0}"
phase30_apply_fail_after="$(json_uint_field "$phase30_health_after" factoryResetApplyFailures)"; phase30_apply_fail_after="${phase30_apply_fail_after:-0}"
phase30_dry_after="$(json_uint_field "$phase30_health_after" factoryResetDryRuns)"; phase30_dry_after="${phase30_dry_after:-0}"
phase30_uptime_after="$(json_uint_field "$phase30_health_after" uptimeMs)"; phase30_uptime_after="${phase30_uptime_after:-0}"

if [[ "$phase30_req_after" -eq $((phase30_req_before + 1)) && \
      "$phase30_fail_after" -eq "$phase30_fail_before" && \
      "$phase30_queued_after" -eq $((phase30_queued_before + 1)) && \
      "$phase30_applied_after" -eq $((phase30_applied_before + 1)) && \
      "$phase30_apply_fail_after" -eq "$phase30_apply_fail_before" && \
      "$phase30_dry_after" -eq $((phase30_dry_before + 1)) ]]; then
  log "PASS Phase 30 factory reset counters: requests/queued/applied/dryRuns +1, failures unchanged"
else
  log "FAIL Phase 30 factory reset counters: req $phase30_req_before->$phase30_req_after fail $phase30_fail_before->$phase30_fail_after queued $phase30_queued_before->$phase30_queued_after applied $phase30_applied_before->$phase30_applied_after applyFail $phase30_apply_fail_before->$phase30_apply_fail_after dry $phase30_dry_before->$phase30_dry_after"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

phase30_box_after="$(curl -sS --max-time 5 "$BASE8080/api/settings/box-name" 2>>"$LOGFILE" || true)"
phase30_box_stored_after="$(json_string_field "$phase30_box_after" stored)"
if [[ "$phase30_uptime_after" -gt "$phase30_uptime_before" && "$phase30_box_stored_after" == "$phase30_box_stored_before" && "$(json_string_field "$phase30_health_after" version)" == "$EXPECTED_FW_VERSION" ]]; then
  log "PASS Phase 30 dry-run did not reboot or clear Preferences; uptime $phase30_uptime_before->$phase30_uptime_after boxName='$phase30_box_stored_after'"
else
  log "FAIL Phase 30 dry-run safety invariant: uptime $phase30_uptime_before->$phase30_uptime_after boxName '$phase30_box_stored_before'->'$phase30_box_stored_after'"
  PHASE30_FACTORY_RESET_FAILURES=$((PHASE30_FACTORY_RESET_FAILURES + 1))
fi

if [[ "$PHASE30_FACTORY_RESET_FAILURES" -eq 0 ]]; then
  log "PHASE 30 FACTORY RESET HANDOFF + AP WIFI PORT-80 EXCEPTION: PASS"
else
  log "PHASE 30 FACTORY RESET HANDOFF + AP WIFI PORT-80 EXCEPTION: FAIL ($PHASE30_FACTORY_RESET_FAILURES problem(s))"
fi
log ""

log "===== PHASE 0V: PHASE-31 ASYNC CAPTIVE PORTAL + TRANSACTIONAL WIFI PROVISIONING CONTRACT ====="
PHASE31_WIFI_PROVISION_FAILURES=0
phase31_info="$(curl -sS --http1.1 --max-time 5 "$BASE8080/api/wifi/provisioning-info" 2>>"$LOGFILE" || true)"
if [[ "$phase31_info" == *'"implementation":"async-captive-portal"'* && \
      "$phase31_info" == *'"port":80'* && \
      "$phase31_info" == *'"transactional":true'* && \
      "$phase31_info" == *'"saveAfterConnect":true'* && \
      "$phase31_info" == *'"showsIpAfterConnect":true'* && \
      "$phase31_info" == *'"languages":["de","en"]'* && \
      "$phase31_info" == *'"browserLanguageFallback":true'* && \
      "$phase31_info" == *'"manualLanguageSwitch":true'* && \
      "$phase31_info" == *'"localizedErrorCodes":true'* && \
      "$phase31_info" == *'"legacyWebServerUsedInApMode":false'* && \
      "$phase31_info" == *'"factoryResetErasesWifi":true'* ]]; then
  log "PASS Phase 31 provisioning contract: Async TCP/80 captive portal, test-before-save, IP display, DE/EN i18n, no legacy WebServer in AP mode"
else
  log "FAIL Phase 31 provisioning contract: $phase31_info"
  PHASE31_WIFI_PROVISION_FAILURES=$((PHASE31_WIFI_PROVISION_FAILURES + 1))
fi

phase31_health="$(fetch_async_health 5 || true)"
if [[ "$phase31_health" == *'"apProvisioningImplementation":"async-captive-portal"'* && \
      "$phase31_health" == *'"legacyWebServerUsedInApMode":false'* && \
      "$phase31_health" == *'"wifiProvisionTransactional":true'* && \
      "$phase31_health" == *'"wifiProvisionShowsIp":true'* && \
      "$phase31_health" == *'"wifiProvisionBilingual":true'* && \
      "$phase31_health" == *'"wifiProvisionBrowserLanguageFallback":true'* && \
      "$phase31_health" == *'"wifiProvisionManualLanguageSwitch":true'* && \
      "$phase31_health" == *'"factoryResetErasesWifi":true'* && \
      "$phase31_health" == *'"stateTransport":"shared-cache-direct-buffer-fixed-length"'* && \
      "$phase31_health" == *'"statePayloadFormat":"compact"'* && \
      "$phase31_health" == *'"stateResponseBufferFailures":0'* ]]; then
  log "PASS Phase 31 health exposes provisioning/factory-reset/i18n invariants"
else
  log "FAIL Phase 31 health provisioning invariants: $phase31_health"
  PHASE31_WIFI_PROVISION_FAILURES=$((PHASE31_WIFI_PROVISION_FAILURES + 1))
fi

# The automated release test must never submit real candidate credentials.
# Verify only that the permanent application port still refuses /save.
phase31_guard_body="${OUTDIR}/phase31-port8080-save-guard.json"
phase31_guard_meta="$(post_empty_capture "phase31" "wifi-save-port8080-guard" "$BASE8080/save" "$phase31_guard_body" 5)"; phase31_guard_rc=$?
IFS=$'\t' read -r phase31_guard_http _ _ <<< "$phase31_guard_meta"
if [[ "$phase31_guard_rc" -eq 0 && "$phase31_guard_http" == "410" && "$(cat "$phase31_guard_body" 2>/dev/null || true)" == *'"ap_wifi_save_port80_only"'* ]]; then
  log "PASS Phase 31 permanent port cannot accept Wi-Fi credentials; provisioning ownership stays TCP/80"
else
  log "FAIL Phase 31 permanent-port Wi-Fi guard: HTTP=${phase31_guard_http:-000} rc=$phase31_guard_rc"
  PHASE31_WIFI_PROVISION_FAILURES=$((PHASE31_WIFI_PROVISION_FAILURES + 1))
fi

if [[ "$PHASE31_WIFI_PROVISION_FAILURES" -eq 0 ]]; then
  log "PHASE 31 ASYNC CAPTIVE PORTAL + TRANSACTIONAL WIFI PROVISIONING: PASS"
else
  log "PHASE 31 ASYNC CAPTIVE PORTAL + TRANSACTIONAL WIFI PROVISIONING: FAIL ($PHASE31_WIFI_PROVISION_FAILURES problem(s))"
fi
log ""

log "===== PHASE 0W: PHASE-32 FINAL LEGACY WEBSERVER REMOVAL + LAST ROUTE HANDOFFS ====="
PHASE32_FINAL_WEB_FAILURES=0
OTA_REAL_CHECK_PASS=0
phase32_health_before="$(fetch_async_health 5 || true)"
phase32_uptime_before="$(json_uint_field "$phase32_health_before" uptimeMs)"; phase32_uptime_before="${phase32_uptime_before:-0}"
phase32_msg_req_before="$(json_uint_field "$phase32_health_before" messageSettingsRequests)"; phase32_msg_req_before="${phase32_msg_req_before:-0}"
phase32_msg_applied_before="$(json_uint_field "$phase32_health_before" messageSettingsApplied)"; phase32_msg_applied_before="${phase32_msg_applied_before:-0}"
phase32_msg_dry_before="$(json_uint_field "$phase32_health_before" messageSettingsDryRuns)"; phase32_msg_dry_before="${phase32_msg_dry_before:-0}"
phase32_grow_req_before="$(json_uint_field "$phase32_health_before" newGrowRequests)"; phase32_grow_req_before="${phase32_grow_req_before:-0}"
phase32_grow_applied_before="$(json_uint_field "$phase32_health_before" newGrowApplied)"; phase32_grow_applied_before="${phase32_grow_applied_before:-0}"
phase32_grow_dry_before="$(json_uint_field "$phase32_health_before" newGrowDryRuns)"; phase32_grow_dry_before="${phase32_grow_dry_before:-0}"

if [[ "$phase32_health_before" == *'"legacyWebServerRemoved":true'* && \
      "$phase32_health_before" == *'"legacyStaCompatibilityServer":false'* && \
      "$phase32_health_before" == *'"messageSettingsAsync":true'* && \
      "$phase32_health_before" == *'"newGrowAsync":true'* && \
      "$phase32_health_before" == *'"otaAsyncWorker":true'* ]]; then
  log "PASS Phase 32 health declares complete synchronous WebServer removal and all four final route migrations"
else
  log "FAIL Phase 32 health contract: $phase32_health_before"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

if probe_port80_closed "phase32-final-listener" "/" "GET"; then
  log "PASS Phase 32 normal STA mode has no TCP/80 listener"
else
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

phase32_js="$(curl -sS --max-time 10 "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&phase32=contract" 2>>"$LOGFILE" || true)"
if [[ "$phase32_js" == *"/savemessagesettings"* &&       "$phase32_js" == *"/api/settings/message-status?requestId="* &&       "$phase32_js" == *"/api/newgrow/status?requestId="* &&       "$phase32_js" == *"/api/ota/check-status?requestId="* &&       "$phase32_js" == *"/api/ota/update-status?requestId="* &&       "$phase32_js" == *'"message.saving": { de:'* &&       "$phase32_js" == *'"ota.checking": { de:'* && \
      "$phase32_js" == *'"ota.reinstallButton": { de:'* && \
      "$phase32_js" == *"window.checkForOtaUpdate = async function"* && \
      "$phase32_js" == *"btn.textContent=otaTr('ota.reinstallButton'"* && \
      "$phase32_js" == *"window.trKey = trKey;"* && \
      "$phase32_js" == *"function bindOtaControls()"* && \
      "$phase32_js" == *"checkBtn.addEventListener('click'"* && \
      "$phase32_js" == *"Date.now()+90000"* ]]; then
  log "PASS Phase 32 dashboard uses Async OTA polling, same-version reinstall, exported translator, explicit DOM click binding, 90s check budget and DE/EN strings"
else
  log "FAIL Phase 32 dashboard route/i18n contract markers missing"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

phase32_html="$(curl -sS --max-time 10 "$BASE8080/" 2>>"$LOGFILE" || true)"
if [[ "$phase32_html" == *'id="otaCheckBtn"'* && \
      "$phase32_html" == *'id="otaInstallBtn"'* && \
      "$phase32_html" != *'onclick="checkForOtaUpdate()"'* && \
      "$phase32_html" != *'onclick="startOtaUpdate()"'* && \
      "$phase32_html" == *"/script.js?v=${TEST_CACHE_BUSTER}"* ]]; then
  log "PASS OTA dashboard buttons use DOM event binding and ${TEST_FW_VERSION} script cache-buster; no inline onclick dependency remains"
else
  log "FAIL OTA dashboard button binding/cache-buster contract missing"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

# Message settings: dry-run exercises queue/main-loop apply without Preferences,
# Pushover/Gotify network calls, or restart.
phase32_msg_body="${OUTDIR}/phase32-message-dryrun.json"
phase32_msg_meta="$(post_empty_capture "phase32" "message-settings-dryrun" "$BASE8080/savemessagesettings?dryRun=1" "$phase32_msg_body" 5)"; phase32_msg_rc=$?
IFS=$'\t' read -r phase32_msg_http _ _ <<< "$phase32_msg_meta"
phase32_msg_resp="$(cat "$phase32_msg_body" 2>/dev/null || true)"
phase32_msg_id="$(json_uint_field "$phase32_msg_resp" requestId)"
if [[ "$phase32_msg_rc" -eq 0 && "$phase32_msg_http" == "202" && -n "$phase32_msg_id" && "$phase32_msg_resp" == *'"dryRun":true'* ]]; then
  phase32_msg_status=""
  for _ in $(seq 1 40); do
    phase32_msg_status="$(curl -sS --max-time 2 "$BASE8080/api/settings/message-status?requestId=${phase32_msg_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase32_msg_status" state)" == "applied" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase32_msg_status" state)" == "applied" ]]; then
    log "PASS message settings dry-run queued and applied outside Async callback requestId=$phase32_msg_id"
  else
    log "FAIL message settings dry-run status: $phase32_msg_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "FAIL message settings dry-run queue: HTTP=${phase32_msg_http:-000} rc=$phase32_msg_rc response='$phase32_msg_resp'"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

# New Grow: dry-run must not alter grow dates/phase or touch Shelly energy counters.
phase32_run_before="$(curl -sS --max-time 5 "$BASE8080/api/settings/run-profile" 2>>"$LOGFILE" || true)"
phase32_start_before="$(json_nested_string_field "$phase32_run_before" runtime startDate)"
phase32_flower_before="$(json_nested_string_field "$phase32_run_before" runtime floweringStart)"
phase32_dry_before_date="$(json_nested_string_field "$phase32_run_before" runtime dryingStart)"
phase32_phase_before="$(json_nested_number_field "$phase32_run_before" runtime currentPhase)"
phase32_grow_body="${OUTDIR}/phase32-newgrow-dryrun.json"
phase32_grow_meta="$(post_empty_capture "phase32" "newgrow-dryrun" "$BASE8080/api/newgrow?dryRun=1" "$phase32_grow_body" 5)"; phase32_grow_rc=$?
IFS=$'\t' read -r phase32_grow_http _ _ <<< "$phase32_grow_meta"
phase32_grow_resp="$(cat "$phase32_grow_body" 2>/dev/null || true)"
phase32_grow_id="$(json_uint_field "$phase32_grow_resp" requestId)"
if [[ "$phase32_grow_rc" -eq 0 && "$phase32_grow_http" == "202" && -n "$phase32_grow_id" && "$phase32_grow_resp" == *'"dryRun":true'* ]]; then
  phase32_grow_status=""
  for _ in $(seq 1 40); do
    phase32_grow_status="$(curl -sS --max-time 2 "$BASE8080/api/newgrow/status?requestId=${phase32_grow_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase32_grow_status" state)" == "applied" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase32_grow_status" state)" == "applied" ]]; then
    log "PASS New Grow dry-run queued and applied outside Async callback requestId=$phase32_grow_id"
  else
    log "FAIL New Grow dry-run status: $phase32_grow_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "FAIL New Grow dry-run queue: HTTP=${phase32_grow_http:-000} rc=$phase32_grow_rc response='$phase32_grow_resp'"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi
sleep 0.2
phase32_run_after="$(curl -sS --max-time 5 "$BASE8080/api/settings/run-profile" 2>>"$LOGFILE" || true)"
if [[ "$(json_nested_string_field "$phase32_run_after" runtime startDate)" == "$phase32_start_before" && \
      "$(json_nested_string_field "$phase32_run_after" runtime floweringStart)" == "$phase32_flower_before" && \
      "$(json_nested_string_field "$phase32_run_after" runtime dryingStart)" == "$phase32_dry_before_date" && \
      "$(json_nested_number_field "$phase32_run_after" runtime currentPhase)" == "$phase32_phase_before" ]]; then
  log "PASS New Grow dry-run left grow dates/phase unchanged"
else
  log "FAIL New Grow dry-run changed grow state"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

# OTA: dry-run validates queue/worker plumbing first. Then a real check-only request
# must reach GitHub and parse manifest.json. Automated tests NEVER call real update/install.
phase32_ota_check_body="${OUTDIR}/phase32-ota-check-dryrun.json"
phase32_ota_check_meta="$(curl -sS --http1.1 --max-time 5 -o "$phase32_ota_check_body" \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/api/ota/check?dryRun=1" 2>>"$LOGFILE")"; phase32_ota_check_rc=$?
record_result "phase32" "ota-check-dryrun" "$phase32_ota_check_meta" "$phase32_ota_check_rc"
IFS=$'\t' read -r phase32_ota_check_http _ _ <<< "$phase32_ota_check_meta"
phase32_ota_check_resp="$(cat "$phase32_ota_check_body" 2>/dev/null || true)"
phase32_ota_check_id="$(json_uint_field "$phase32_ota_check_resp" requestId)"
if [[ "$phase32_ota_check_rc" -eq 0 && "$phase32_ota_check_http" == "202" && -n "$phase32_ota_check_id" && "$phase32_ota_check_resp" == *'"dryRun":true'* ]]; then
  phase32_ota_check_status=""
  for _ in $(seq 1 60); do
    phase32_ota_check_status="$(curl -sS --max-time 2 "$BASE8080/api/ota/check-status?requestId=${phase32_ota_check_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase32_ota_check_status" state)" == "done" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase32_ota_check_status" state)" == "done" && \
        "$(json_bool_field "$phase32_ota_check_status" dryRun)" == "true" && \
        "$(json_string_field "$phase32_ota_check_status" currentVersion)" == "$EXPECTED_FW_VERSION" ]]; then
    log "PASS OTA check dry-run completed in worker without network requestId=$phase32_ota_check_id"
  else
    log "FAIL OTA check dry-run status: $phase32_ota_check_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "FAIL OTA check dry-run queue: HTTP=${phase32_ota_check_http:-000} rc=$phase32_ota_check_rc response='$phase32_ota_check_resp'"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi
# The status can become done a few scheduler ticks before the worker task
# clears its task handle. Give the worker time to exit before queueing the real check.
sleep 0.3

# Real OTA check-only hardware gate. This performs HTTPS + GitHub redirect +
# manifest JSON parsing, but NEVER downloads or flashes firmware.bin.
phase32_ota_real_body="${OUTDIR}/phase32-ota-check-real.json"
phase32_ota_real_meta="$(curl -sS --http1.1 --max-time 5 -o "$phase32_ota_real_body" \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/api/ota/check" 2>>"$LOGFILE")"; phase32_ota_real_rc=$?
record_result "phase32" "ota-check-real" "$phase32_ota_real_meta" "$phase32_ota_real_rc"
IFS=$'\t' read -r phase32_ota_real_http _ _ <<< "$phase32_ota_real_meta"
phase32_ota_real_resp="$(cat "$phase32_ota_real_body" 2>/dev/null || true)"
phase32_ota_real_id="$(json_uint_field "$phase32_ota_real_resp" requestId)"
if [[ "$phase32_ota_real_rc" -eq 0 && "$phase32_ota_real_http" == "202" && -n "$phase32_ota_real_id" && "$phase32_ota_real_resp" == *'"dryRun":false'* ]]; then
  # v0.4.17 regression: queueing a new OTA job must not invalidate the terminal
  # requestId of the previous OTA check. The old dry-run request must remain
  # queryable instead of returning request_not_found.
  phase32_ota_archived_status="$(curl -sS --max-time 3 "$BASE8080/api/ota/check-status?requestId=${phase32_ota_check_id}" 2>>"$LOGFILE" || true)"
  if [[ "$(json_bool_field "$phase32_ota_archived_status" ok)" == "true" &&         "$(json_string_field "$phase32_ota_archived_status" state)" == "done" &&         "$phase32_ota_archived_status" != *'"request_not_found"'* ]]; then
    log "PASS OTA terminal requestId remains queryable after next OTA job queue: oldId=$phase32_ota_check_id newId=$phase32_ota_real_id"
  else
    log "FAIL OTA terminal requestId archive regression: $phase32_ota_archived_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi

  phase32_ota_real_status=""
  for _ in $(seq 1 180); do
    phase32_ota_real_status="$(curl -sS --max-time 3 "$BASE8080/api/ota/check-status?requestId=${phase32_ota_real_id}" 2>>"$LOGFILE" || true)"
    phase32_ota_real_state="$(json_string_field "$phase32_ota_real_status" state)"
    [[ "$phase32_ota_real_state" == "done" || "$phase32_ota_real_state" == "failed" ]] && break
    sleep 0.5
  done
  phase32_ota_real_latest="$(json_string_field "$phase32_ota_real_status" latestVersion)"
  phase32_ota_real_httpcode="$(json_number_field "$phase32_ota_real_status" manifestHttpCode)"
  phase32_ota_real_length="$(json_number_field "$phase32_ota_real_status" manifestContentLength)"
  phase32_ota_real_received="$(json_number_field "$phase32_ota_real_status" manifestReceivedLength)"
  phase32_ota_real_attempt="$(json_number_field "$phase32_ota_real_status" attempt)"
  if [[ "$(json_string_field "$phase32_ota_real_status" state)" == "done" && \
        "$(json_bool_field "$phase32_ota_real_status" dryRun)" == "false" && \
        "$(json_string_field "$phase32_ota_real_status" currentVersion)" == "$EXPECTED_FW_VERSION" && \
        -n "$phase32_ota_real_latest" && "$phase32_ota_real_httpcode" == "200" && \
        -n "$phase32_ota_real_received" && "$phase32_ota_real_received" -gt 0 && \
        ( "$phase32_ota_real_length" == "-1" || "$phase32_ota_real_received" == "$phase32_ota_real_length" ) ]]; then
    OTA_REAL_CHECK_PASS=1
    log "PASS REAL OTA GitHub check-only: requestId=$phase32_ota_real_id latest=$phase32_ota_real_latest HTTP=$phase32_ota_real_httpcode bytes=$phase32_ota_real_received/$phase32_ota_real_length attempt=${phase32_ota_real_attempt:-0}"
  else
    log "FAIL REAL OTA GitHub check-only status: $phase32_ota_real_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "FAIL REAL OTA GitHub check-only queue: HTTP=${phase32_ota_real_http:-000} rc=$phase32_ota_real_rc response='$phase32_ota_real_resp'"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

# Wait for the real check worker to release its task handle before the safe
# update dry-run. No real update endpoint is ever called by this test.
sleep 0.3

phase32_ota_update_body="${OUTDIR}/phase32-ota-update-dryrun.json"
phase32_ota_update_meta="$(post_empty_capture "phase32" "ota-update-dryrun" "$BASE8080/api/ota/update?dryRun=1" "$phase32_ota_update_body" 5)"; phase32_ota_update_rc=$?
IFS=$'\t' read -r phase32_ota_update_http _ _ <<< "$phase32_ota_update_meta"
phase32_ota_update_resp="$(cat "$phase32_ota_update_body" 2>/dev/null || true)"
phase32_ota_update_id="$(json_uint_field "$phase32_ota_update_resp" requestId)"
if [[ "$phase32_ota_update_rc" -eq 0 && "$phase32_ota_update_http" == "202" && -n "$phase32_ota_update_id" && "$phase32_ota_update_resp" == *'"dryRun":true'* ]]; then
  phase32_ota_update_status=""
  for _ in $(seq 1 60); do
    phase32_ota_update_status="$(curl -sS --max-time 2 "$BASE8080/api/ota/update-status?requestId=${phase32_ota_update_id}" 2>>"$LOGFILE" || true)"
    [[ "$(json_string_field "$phase32_ota_update_status" state)" == "done" ]] && break
    sleep 0.1
  done
  if [[ "$(json_string_field "$phase32_ota_update_status" state)" == "done" && \
        "$(json_bool_field "$phase32_ota_update_status" dryRun)" == "true" && \
        "$(json_string_field "$phase32_ota_update_status" message)" == "dry_run_complete" ]]; then
    log "PASS OTA update dry-run completed in worker without flash/reboot requestId=$phase32_ota_update_id"
  else
    log "FAIL OTA update dry-run status: $phase32_ota_update_status"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "FAIL OTA update dry-run queue: HTTP=${phase32_ota_update_http:-000} rc=$phase32_ota_update_rc response='$phase32_ota_update_resp'"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

sleep 0.3
phase32_health_after="$(fetch_async_health 5 || true)"
phase32_uptime_after="$(json_uint_field "$phase32_health_after" uptimeMs)"; phase32_uptime_after="${phase32_uptime_after:-0}"
phase32_msg_req_after="$(json_uint_field "$phase32_health_after" messageSettingsRequests)"; phase32_msg_req_after="${phase32_msg_req_after:-0}"
phase32_msg_applied_after="$(json_uint_field "$phase32_health_after" messageSettingsApplied)"; phase32_msg_applied_after="${phase32_msg_applied_after:-0}"
phase32_msg_dry_after="$(json_uint_field "$phase32_health_after" messageSettingsDryRuns)"; phase32_msg_dry_after="${phase32_msg_dry_after:-0}"
phase32_grow_req_after="$(json_uint_field "$phase32_health_after" newGrowRequests)"; phase32_grow_req_after="${phase32_grow_req_after:-0}"
phase32_grow_applied_after="$(json_uint_field "$phase32_health_after" newGrowApplied)"; phase32_grow_applied_after="${phase32_grow_applied_after:-0}"
phase32_grow_dry_after="$(json_uint_field "$phase32_health_after" newGrowDryRuns)"; phase32_grow_dry_after="${phase32_grow_dry_after:-0}"

if [[ "$phase32_msg_req_after" -eq $((phase32_msg_req_before + 1)) && \
      "$phase32_msg_applied_after" -eq $((phase32_msg_applied_before + 1)) && \
      "$phase32_msg_dry_after" -eq $((phase32_msg_dry_before + 1)) ]]; then
  log "PASS message settings Phase 32 counters: request/applied/dryRun +1"
else
  log "FAIL message settings counters: req $phase32_msg_req_before->$phase32_msg_req_after applied $phase32_msg_applied_before->$phase32_msg_applied_after dry $phase32_msg_dry_before->$phase32_msg_dry_after"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi
if [[ "$phase32_grow_req_after" -eq $((phase32_grow_req_before + 1)) && \
      "$phase32_grow_applied_after" -eq $((phase32_grow_applied_before + 1)) && \
      "$phase32_grow_dry_after" -eq $((phase32_grow_dry_before + 1)) ]]; then
  log "PASS New Grow Phase 32 counters: request/applied/dryRun +1"
else
  log "FAIL New Grow counters: req $phase32_grow_req_before->$phase32_grow_req_after applied $phase32_grow_applied_before->$phase32_grow_applied_after dry $phase32_grow_dry_before->$phase32_grow_dry_after"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi
if [[ "$phase32_uptime_after" -gt "$phase32_uptime_before" && "$(json_string_field "$phase32_health_after" version)" == "$EXPECTED_FW_VERSION" ]]; then
  log "PASS all Phase 32 dry-runs completed without reboot; uptime $phase32_uptime_before->$phase32_uptime_after"
else
  log "FAIL Phase 32 reboot guard: uptime $phase32_uptime_before->$phase32_uptime_after"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

if [[ "$PHASE32_FINAL_WEB_FAILURES" -eq 0 ]]; then
  log "PHASE 32 FINAL LEGACY WEBSERVER REMOVAL + LAST ROUTE HANDOFFS + REAL OTA CHECK: PASS"
else
  log "PHASE 32 FINAL LEGACY WEBSERVER REMOVAL + LAST ROUTE HANDOFFS + REAL OTA CHECK: FAIL ($PHASE32_FINAL_WEB_FAILURES problem(s))"
fi
log ""

for spec in \
  "async-root|$BASE8080/" \
  "async-state|$BASE8080/api/state" \
  "async-stats|$BASE8080/api/stats" \
  "async-hint|$BASE8080/api/hint" \
  "async-fs-status|$BASE8080/api/fs/status" \
  "async-logbuffer|$BASE8080/api/logbuffer" \
  "async-log-download|$BASE8080/download/log" \
  "async-diary-list|$BASE8080/api/diary/list" \
  "async-diary-csv|$BASE8080/api/diary.csv" \
  "async-theme|$BASE8080/api/settings/theme" \
  "async-language|$BASE8080/api/settings/language" \
  "async-unit|$BASE8080/api/settings/unit" \
  "async-time-format|$BASE8080/api/settings/time-format" \
  "async-box-name|$BASE8080/api/settings/box-name" \
  "async-debug|$BASE8080/api/settings/debug" \
  "async-ds18b20-name|$BASE8080/api/settings/ds18b20-name" \
  "async-power-price|$BASE8080/api/settings/power-price" \
  "async-ntp-server|$BASE8080/api/settings/ntp-server" \
  "async-time-zone|$BASE8080/api/settings/time-zone" \
  "async-ds18b20-enabled|$BASE8080/api/settings/ds18b20-enabled" \
  "async-active-relay-count|$BASE8080/api/settings/active-relay-count" \
  "async-run-profile|$BASE8080/api/settings/run-profile" \
  "async-irrigation-config|$BASE8080/api/settings/irrigation" \
  "async-grow-targets|$BASE8080/api/settings/grow-targets" \
  "async-heating-tuning|$BASE8080/api/settings/heating-tuning" \
  "async-heating-output|$BASE8080/api/settings/heating-output" \
  "async-light-schedule|$BASE8080/api/settings/light-schedule" \
  "async-relay-schedule-1|$BASE8080/api/settings/relay-schedule/1" \
  "async-relay-action-1|$BASE8080/api/relay/1/state" \
  "async-watering-state|$BASE8080/api/watering/state" \
  "async-storage-status|$BASE8080/api/storage/action-status?requestId=0" \
  "async-shelly-main|$BASE8080/api/settings/shelly/main" \
  "async-shelly-auth|$BASE8080/api/settings/shelly/auth" \
  "async-system-restart-status|$BASE8080/api/system/restart-status?requestId=0" \
  "async-factory-reset-status|$BASE8080/api/system/factory-reset-status?requestId=0" \
  "async-wifi-provisioning-info|$BASE8080/api/wifi/provisioning-info" \
  "async-relay-name-1|$BASE8080/api/settings/relay-name/1" \
  "async-relay-name-5|$BASE8080/api/settings/relay-name/5" \
  "async-health|$BASE8080/api/async/health" \
  "async-favicon|$BASE8080/favicon.ico"
do
  label="${spec%%|*}"
  url="${spec#*|}"
  line="$(one_request "smoke" "$label" "$url" 5)"
  log "$line"
done

log ""
log "Async health before load:"
curl -sS --max-time 5 "$BASE8080/api/async/health" 2>&1 | tee -a "$LOGFILE" || true
log ""

log "===== PHASE 1: SERIAL ASYNC API BASELINE (20 rounds) ====="
for i in $(seq 1 20); do
  a="$(one_request "serial" "async-state" "$BASE8080/api/state" 5)"
  b="$(one_request "serial" "async-stats" "$BASE8080/api/stats" 5)"
  log "round=$i  $a  |  $b"
  sleep 0.25
done

log ""
PHASE2_ASYNC_FAILURES=0
PHASE3_ASYNC_FAILURES=0
log "===== PHASE 2: PARALLEL ASYNC STATE + STATS (30 rounds) ====="
for i in $(seq 1 30); do
  f1="${OUTDIR}/p2-${i}-state.tmp"
  f2="${OUTDIR}/p2-${i}-stats.tmp"
  parallel_request_to_file "$BASE8080/api/state" 5 "$f1" & p1=$!
  parallel_request_to_file "$BASE8080/api/stats" 5 "$f2" & p2=$!
  wait "$p1" "$p2" 2>/dev/null || true
  collect_parallel "parallel-async" "async-state" "$f1"
  collect_parallel "parallel-async" "async-stats" "$f2"
  # print the just-recorded pair for human readability
  tail -n 2 "$DATAFILE" | awk -F '\t' '{printf "%s HTTP=%s total=%ss rc=%s  ", $3,$4,$5,$7} END{print ""}' | tee -a "$LOGFILE"
  sleep 0.25
done
PHASE2_ASYNC_FAILURES="$(awk -F '\t' '$2=="parallel-async" && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300 || ($6+0)<5000) {n++} END{print n+0}' "$DATAFILE")"
if [[ "$PHASE2_ASYNC_FAILURES" -eq 0 ]]; then
  log "PHASE 2 PARALLEL ASYNC RESULT: PASS"
else
  log "PHASE 2 PARALLEL ASYNC RESULT: FAIL ($PHASE2_ASYNC_FAILURES request failure(s))"
fi

log ""
log "===== PHASE 3: ASYNC MIXED PARALLEL LOAD (state + stats + root, 20 rounds) ====="
for i in $(seq 1 20); do
  f1="${OUTDIR}/p3-${i}-state.tmp"
  f2="${OUTDIR}/p3-${i}-stats.tmp"
  f3="${OUTDIR}/p3-${i}-root.tmp"
  parallel_request_to_file "$BASE8080/api/state" 5 "$f1" & p1=$!
  parallel_request_to_file "$BASE8080/api/stats" 5 "$f2" & p2=$!
  parallel_request_to_file "$BASE8080/" 5 "$f3" & p3=$!
  wait "$p1" "$p2" "$p3" 2>/dev/null || true
  collect_parallel "mixed-async" "async-state" "$f1"
  collect_parallel "mixed-async" "async-stats" "$f2"
  collect_parallel "mixed-async" "async-root" "$f3"
  tail -n 3 "$DATAFILE" | awk -F '\t' '{printf "%s HTTP=%s total=%ss rc=%s  ", $3,$4,$5,$7} END{print ""}' | tee -a "$LOGFILE"
  sleep 0.25
done
PHASE3_ASYNC_FAILURES="$(awk -F '\t' '$2=="mixed-async" && (($3=="async-state" || $3=="async-stats") && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300 || ($6+0)<5000) || $3=="async-root" && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300 || ($6+0)<45000)) {n++} END{print n+0}' "$DATAFILE")"
if [[ "$PHASE3_ASYNC_FAILURES" -eq 0 ]]; then
  log "PHASE 3 MIXED ASYNC RESULT: PASS"
else
  log "PHASE 3 MIXED ASYNC RESULT: FAIL ($PHASE3_ASYNC_FAILURES async request failure(s))"
fi

log ""
log "===== PHASE 4: ASYNC BURST LOAD (5 rounds x 8 simultaneous requests) ====="
log "Burst diagnostics r2: any failed request preserves body/headers/stderr and logs round, slot, endpoint, HTTP, curl rc, timing and byte counts."
for round in $(seq 1 5); do
  pids=()
  files=()
  labels=()
  slots=()
  endpoints=()
  for n in $(seq 1 4); do
    f="${OUTDIR}/p4-${round}-state-${n}.tmp"
    parallel_burst_request_to_file "$BASE8080/api/state" 5 "$f" & pids+=("$!")
    files+=("$f"); labels+=("async-state"); slots+=("state-$n"); endpoints+=("/api/state")
  done
  for n in $(seq 1 4); do
    f="${OUTDIR}/p4-${round}-stats-${n}.tmp"
    parallel_burst_request_to_file "$BASE8080/api/stats" 5 "$f" & pids+=("$!")
    files+=("$f"); labels+=("async-stats"); slots+=("stats-$n"); endpoints+=("/api/stats")
  done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
  for idx in "${!files[@]}"; do
    collect_burst_parallel "burst" "${labels[$idx]}" "${files[$idx]}" "$round" "${slots[$idx]}" "${endpoints[$idx]}"
  done
  log "burst round $round complete"
  sleep 0.5
done
PHASE4_ASYNC_FAILURES="$(awk -F '\t' '$2=="burst" && ($3=="async-state" || $3=="async-stats") && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300 || ($6+0)<5000) {n++} END{print n+0}' "$DATAFILE")"
if [[ "$PHASE4_ASYNC_FAILURES" -eq 0 ]]; then
  log "PHASE 4 ASYNC BURST RESULT: PASS"
else
  log "PHASE 4 ASYNC BURST RESULT: FAIL ($PHASE4_ASYNC_FAILURES request/body failure(s))"
fi

log ""
log "===== PHASE 5A: COLD DASHBOARD LOAD (5 rounds) ====="
log "Cold-cache stress: HTML, CSS, JS, state, stats, hint and logbuffer concurrently."
for round in $(seq 1 5); do
  urls=(
    "$BASE8080/"
    "$BASE8080/style.css?v=${TEST_CACHE_BUSTER}"
    "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}"
    "$BASE8080/api/state"
    "$BASE8080/api/stats"
    "$BASE8080/api/hint"
    "$BASE8080/api/logbuffer"
  )
  labels=("async-root" "async-css" "async-js" "async-state" "async-stats" "async-hint" "async-logbuffer")
  pids=(); files=()
  for idx in "${!urls[@]}"; do
    f="${OUTDIR}/p5a-${round}-${idx}.tmp"
    parallel_request_to_file "${urls[$idx]}" 8 "$f" & pids+=("$!")
    files+=("$f")
  done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
  for idx in "${!files[@]}"; do
    collect_parallel "dashboard-cold" "${labels[$idx]}" "${files[$idx]}"
  done
  tail -n 7 "$DATAFILE" | awk -F '\t' '{printf "%s=%ss(%s) ", $3,$5,$4} END{print ""}' | tee -a "$LOGFILE"
  sleep 0.75
done

log ""
log "===== PHASE 5B: WARM DASHBOARD LOAD (10 rounds) ====="
log "Warm-cache simulation: browser already has versioned CSS/JS, so only HTML + APIs are fetched."
for round in $(seq 1 10); do
  urls=(
    "$BASE8080/"
    "$BASE8080/api/state"
    "$BASE8080/api/stats"
    "$BASE8080/api/hint"
    "$BASE8080/api/logbuffer"
  )
  labels=("warm-root" "warm-state" "warm-stats" "warm-hint" "warm-logbuffer")
  pids=(); files=()
  for idx in "${!urls[@]}"; do
    f="${OUTDIR}/p5b-${round}-${idx}.tmp"
    parallel_request_to_file "${urls[$idx]}" 8 "$f" & pids+=("$!")
    files+=("$f")
  done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
  for idx in "${!files[@]}"; do
    collect_parallel "dashboard-warm" "${labels[$idx]}" "${files[$idx]}"
  done
  tail -n 5 "$DATAFILE" | awk -F '\t' '{printf "%s=%ss(%s) ", $3,$5,$4} END{print ""}' | tee -a "$LOGFILE"
  sleep 0.5
done

log ""
log "===== PHASE 5C: READ-ONLY DIARY ENDPOINTS (10 parallel rounds) ====="
for i in $(seq 1 10); do
  f1="${OUTDIR}/p5c-${i}-list.tmp"
  f2="${OUTDIR}/p5c-${i}-csv.tmp"
  parallel_request_to_file "$BASE8080/api/diary/list" 5 "$f1" & p1=$!
  parallel_request_to_file "$BASE8080/api/diary.csv" 5 "$f2" & p2=$!
  wait "$p1" "$p2" 2>/dev/null || true
  collect_parallel "diary-read" "async-diary-list" "$f1"
  collect_parallel "diary-read" "async-diary-csv" "$f2"
  tail -n 2 "$DATAFILE" | awk -F '\t' '{printf "%s HTTP=%s total=%ss bytes=%s rc=%s  ", $3,$4,$5,$6,$7} END{print ""}' | tee -a "$LOGFILE"
  sleep 0.25
done

PHASE5A_ASYNC_FAILURES="$(awk -F '\t' '$2=="dashboard-cold" && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300) {n++} END{print n+0}' "$DATAFILE")"
PHASE5B_ASYNC_FAILURES="$(awk -F '\t' '$2=="dashboard-warm" && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300) {n++} END{print n+0}' "$DATAFILE")"
PHASE5C_ASYNC_FAILURES="$(awk -F '\t' '$2=="diary-read" && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300) {n++} END{print n+0}' "$DATAFILE")"
if [[ "$PHASE5A_ASYNC_FAILURES" -eq 0 && "$PHASE5B_ASYNC_FAILURES" -eq 0 && "$PHASE5C_ASYNC_FAILURES" -eq 0 ]]; then
  log "PHASE 5 DASHBOARD/DIARY LOAD RESULT: PASS"
else
  log "PHASE 5 DASHBOARD/DIARY LOAD RESULT: FAIL (cold=$PHASE5A_ASYNC_FAILURES warm=$PHASE5B_ASYNC_FAILURES diary=$PHASE5C_ASYNC_FAILURES)"
fi

log ""
LOGCLEAR_FAILURES=0
log "===== PHASE 5D: REAL ASYNC WRITE ROUTE /api/logbuffer/clear ====="
preclear_file="${OUTDIR}/weblog-before-async-clear.txt"
curl -sS --max-time 5 "$BASE8080/api/logbuffer" -o "$preclear_file" 2>>"$LOGFILE" || true
preclear_bytes="$(wc -c < "$preclear_file" | tr -d ' ')"

# v0.4.20 credential-log hardening: labels may be present, values must not be.
# Do not print the matching line on failure because an older/incompatible build
# could contain a real credential after "read =".
if grep -Eq '\[PREFERENCES LOAD\] (pushoverAppKey|pushoverUserKey|pushoverDevice|gotifyToken) read = ' "$preclear_file"; then
  log "FAIL notification credential value detected in system log"
  LOGCLEAR_FAILURES=$((LOGCLEAR_FAILURES + 1))
else
  log "PASS notification credential values are hidden in system log"
fi
health_before_clear="$(fetch_async_health 5 || true)"
clear_count_before="$(printf '%s' "$health_before_clear" | sed -n 's/.*"logClearRequests":\([0-9][0-9]*\).*/\1/p')"
clear_count_before="${clear_count_before:-0}"

clear_headers="${OUTDIR}/log-clear.headers"
clear_meta="$(curl -sS -o /dev/null -D "$clear_headers" --max-time 5 \
  -X POST -w '%{http_code}\t%{time_total}\t%{size_download}' \
  "$BASE8080/api/logbuffer/clear" 2>>"$LOGFILE")"
clear_rc=$?
record_result "write-logclear" "async-log-clear" "$clear_meta" "$clear_rc"
IFS=$'\t' read -r clear_http clear_seconds clear_bytes <<< "$clear_meta"
cleared_lines="$(awk 'BEGIN{IGNORECASE=1} /^X-GrowTent-Log-Cleared-Lines:/ {sub(/^[^:]*:[[:space:]]*/, ""); sub(/\r$/, ""); print; exit}' "$clear_headers")"

health_after_clear="$(fetch_async_health 5 || true)"
clear_count_after="$(printf '%s' "$health_after_clear" | sed -n 's/.*"logClearRequests":\([0-9][0-9]*\).*/\1/p')"
clear_count_after="${clear_count_after:-0}"

postclear_file="${OUTDIR}/weblog-immediately-after-async-clear.txt"
curl -sS --max-time 5 "$BASE8080/api/logbuffer" -o "$postclear_file" 2>>"$LOGFILE" || true
postclear_bytes="$(wc -c < "$postclear_file" | tr -d ' ')"

if [[ "$clear_rc" -eq 0 && "$clear_http" == "204" ]]; then
  log "PASS async log clear HTTP: 204"
else
  log "FAIL async log clear HTTP: HTTP=${clear_http:-000} rc=$clear_rc"
  LOGCLEAR_FAILURES=$((LOGCLEAR_FAILURES + 1))
fi

if [[ "$clear_count_after" -eq $((clear_count_before + 1)) ]]; then
  log "PASS log-clear counter incremented: $clear_count_before -> $clear_count_after"
else
  log "FAIL log-clear counter: before=$clear_count_before after=$clear_count_after"
  LOGCLEAR_FAILURES=$((LOGCLEAR_FAILURES + 1))
fi

if [[ "$cleared_lines" =~ ^[0-9]+$ ]]; then
  log "PASS clear response header: clearedLines=$cleared_lines"
else
  log "FAIL clear response header missing/invalid: '${cleared_lines:-<missing>}'"
  LOGCLEAR_FAILURES=$((LOGCLEAR_FAILURES + 1))
fi

log "Web log bytes before clear=$preclear_bytes immediately-after=$postclear_bytes"
log "Pre-clear log preserved in: $preclear_file"
if [[ "$LOGCLEAR_FAILURES" -eq 0 ]]; then
  log "ASYNC REAL WRITE ROUTE CHECK: PASS"
else
  log "ASYNC REAL WRITE ROUTE CHECK: FAIL ($LOGCLEAR_FAILURES problem(s))"
fi

log ""
log "===== PHASE 6: RECOVERY CHECK (10 parallel rounds) ====="
for i in $(seq 1 10); do
  f1="${OUTDIR}/p6-${i}-state.tmp"
  f2="${OUTDIR}/p6-${i}-stats.tmp"
  parallel_request_to_file "$BASE8080/api/state" 5 "$f1" & p1=$!
  parallel_request_to_file "$BASE8080/api/stats" 5 "$f2" & p2=$!
  wait "$p1" "$p2" 2>/dev/null || true
  collect_parallel "recovery" "async-state" "$f1"
  collect_parallel "recovery" "async-stats" "$f2"
  sleep 0.25
done
PHASE6_ASYNC_FAILURES="$(awk -F '\t' '$2=="recovery" && ($3=="async-state" || $3=="async-stats") && (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300 || ($6+0)<5000) {n++} END{print n+0}' "$DATAFILE")"
if [[ "$PHASE6_ASYNC_FAILURES" -eq 0 ]]; then
  log "PHASE 6 RECOVERY RESULT: PASS"
else
  log "PHASE 6 RECOVERY RESULT: FAIL ($PHASE6_ASYNC_FAILURES request/body failure(s))"
fi

log ""
log "Async health after load:"
curl -sS --max-time 5 "$BASE8080/api/async/health" 2>&1 | tee -a "$LOGFILE" || true
log ""

# Human-readable aggregate summary. Smoke checks are intentionally excluded from
# timing aggregates because some diagnostic endpoints may legitimately return a
# non-200 status (for example FS status when LittleFS is not mounted).
awk -F '\t' '
BEGIN {
  printf "GrowTent AsyncWebServer timing summary\n"
  printf "Generated from non-smoke requests only.\n\n"
  printf "%-18s %7s %8s %8s %8s %8s %8s %8s %8s\n", "label", "count", "min_ms", "avg_ms", "max_ms", ">500ms", ">1s", ">5s", "fail"
}
NR==1 || $2=="smoke" || $2=="asset-check" || $2=="negative-test" { next }
{
  label=$3
  t=$5+0
  ms=t*1000
  count[label]++
  sum[label]+=ms
  if (!(label in min) || ms<min[label]) min[label]=ms
  if (!(label in max) || ms>max[label]) max[label]=ms
  if (t>0.5) over500[label]++
  if (t>1.0) over1[label]++
  if (t>5.0) over5[label]++
  if (($7+0)!=0 || ($4+0)<200 || ($4+0)>=300) fail[label]++
}
END {
  for (label in count) {
    printf "%-18s %7d %8.1f %8.1f %8.1f %8d %8d %8d %8d\n", label, count[label], min[label], sum[label]/count[label], max[label], over500[label]+0, over1[label]+0, over5[label]+0, fail[label]+0
  }
}
' "$DATAFILE" > "$SUMMARYFILE"

log "===== SUMMARY ====="
cat "$SUMMARYFILE" | tee -a "$LOGFILE"

log ""
log "Potential outliers (>500 ms or request failure):"
awk -F '\t' 'NR>1 && $2!="negative-test" && ($5+0>0.5 || $7+0!=0 || $4+0<200 || $4+0>=300) {print}' "$DATAFILE" \
  | tee -a "$LOGFILE" || true

log ""
if [[ "$LEGACY_UI_FAILURES" -eq 0 ]]; then
  log "Normal-STA TCP/80 listener removal result: PASS"
else
  log "Normal-STA TCP/80 listener removal result: FAIL ($LEGACY_UI_FAILURES problem(s))"
fi

log ""
if [[ "$ASSET_FAILURES" -eq 0 ]]; then
  log "Static asset integrity result: PASS"
else
  log "Static asset integrity result: FAIL ($ASSET_FAILURES problem(s))"
fi

log ""
if [[ "$CACHE_FAILURES" -eq 0 ]]; then
  log "Cache header policy result: PASS"
else
  log "Cache header policy result: FAIL ($CACHE_FAILURES problem(s))"
fi

log ""
if [[ "$READCACHE_FAILURES" -eq 0 ]]; then
  log "Async state cache result: PASS"
else
  log "Async state cache result: FAIL ($READCACHE_FAILURES problem(s))"
fi

log ""
if [[ "$LOGCLEAR_FAILURES" -eq 0 ]]; then
  log "Async real write route result: PASS"
else
  log "Async real write route result: FAIL ($LOGCLEAR_FAILURES problem(s))"
fi

log ""
if [[ "$PREFWRITE_FAILURES" -eq 0 ]]; then
  log "Async safe system + restart-applied + Shelly settings write/restore result: PASS"
else
  log "Async safe system + restart-applied + Shelly settings write/restore result: FAIL ($PREFWRITE_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_GROW_FAILURES" -eq 0 ]]; then
  log "Runtime grow-target main-loop handoff result: PASS"
else
  log "Runtime grow-target main-loop handoff result: FAIL ($RUNTIME_GROW_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_HEAT_FAILURES" -eq 0 ]]; then
  log "Runtime heating-tuning main-loop handoff result: PASS"
else
  log "Runtime heating-tuning main-loop handoff result: FAIL ($RUNTIME_HEAT_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_HEAT_OUTPUT_FAILURES" -eq 0 ]]; then
  log "Runtime heating source/output main-loop handoff result: PASS"
else
  log "Runtime heating source/output main-loop handoff result: FAIL ($RUNTIME_HEAT_OUTPUT_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_LIGHT_FAILURES" -eq 0 ]]; then
  log "Runtime grow-light schedule main-loop handoff result: PASS"
else
  log "Runtime grow-light schedule main-loop handoff result: FAIL ($RUNTIME_LIGHT_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_RELAY_SCHEDULE_FAILURES" -eq 0 ]]; then
  log "Runtime relay-schedule main-loop handoff result: PASS"
else
  log "Runtime relay-schedule main-loop handoff result: FAIL ($RUNTIME_RELAY_SCHEDULE_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_RELAY_ACTION_FAILURES" -eq 0 ]]; then
  log "Runtime relay action main-loop handoff result: PASS"
else
  log "Runtime relay action main-loop handoff result: FAIL ($RUNTIME_RELAY_ACTION_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_WATERING_FAILURES" -eq 0 ]]; then
  log "Runtime irrigation/Flood main-loop handoff result: PASS"
else
  log "Runtime irrigation/Flood main-loop handoff result: FAIL ($RUNTIME_WATERING_FAILURES problem(s))"
fi

log ""
if [[ "$RUNTIME_STORAGE_FAILURES" -eq 0 ]]; then
  log "Runtime diary/LittleFS main-loop handoff result: PASS"
else
  log "Runtime diary/LittleFS main-loop handoff result: FAIL ($RUNTIME_STORAGE_FAILURES problem(s))"
fi

if [[ "$PHASE26_SETTINGS_FAILURES" -eq 0 ]]; then
  log "Phase 26 split Betriebseinstellungen save result: PASS"
else
  log "Phase 26 split Betriebseinstellungen save result: FAIL ($PHASE26_SETTINGS_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE27_SHELLY_FAILURES" -eq 0 ]]; then
  log "Phase 27 Shelly runtime action handoff result: PASS"
else
  log "Phase 27 Shelly runtime action handoff result: FAIL ($PHASE27_SHELLY_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE28_SHELLY_SETTINGS_FAILURES" -eq 0 ]]; then
  log "Phase 28 Shelly dashboard settings save result: PASS"
else
  log "Phase 28 Shelly dashboard settings save result: FAIL ($PHASE28_SHELLY_SETTINGS_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE29_SYSTEM_SETTINGS_FAILURES" -eq 0 ]]; then
  log "Phase 29 System Settings dashboard/restart result: PASS"
else
  log "Phase 29 System Settings dashboard/restart result: FAIL ($PHASE29_SYSTEM_SETTINGS_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE30_FACTORY_RESET_FAILURES" -eq 0 ]]; then
  log "Phase 30 Factory Reset/AP Wi-Fi port ownership result: PASS"
else
  log "Phase 30 Factory Reset/AP Wi-Fi port ownership result: FAIL ($PHASE30_FACTORY_RESET_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE31_WIFI_PROVISION_FAILURES" -eq 0 ]]; then
  log "Phase 31 Async captive portal/transactional Wi-Fi provisioning result: PASS"
else
  log "Phase 31 Async captive portal/transactional Wi-Fi provisioning result: FAIL ($PHASE31_WIFI_PROVISION_FAILURES problem(s))"
fi
log ""
if [[ "$PHASE32_FINAL_WEB_FAILURES" -eq 0 ]]; then
  log "Phase 32 final legacy WebServer removal/last route handoffs result: PASS"
else
  log "Phase 32 final legacy WebServer removal/last route handoffs result: FAIL ($PHASE32_FINAL_WEB_FAILURES problem(s))"
fi
if [[ "$OTA_REAL_CHECK_PASS" -eq 1 ]]; then
  log "Real OTA GitHub manifest check-only result: PASS"
else
  log "Real OTA GitHub manifest check-only result: FAIL"
fi
log ""
if [[ "$ASSET_TRANSPORT_FAILURES" -eq 0 && "$ASSET_TRANSPORT_TIMEOUTS" -eq 0 && "$ASSET_TRANSPORT_INCOMPLETE" -eq 0 && "$ASSET_TRANSPORT_HEADER_ERRORS" -eq 0 ]]; then
  log "Static asset direct-buffer fixed-length transport result: PASS"
else
  log "Static asset direct-buffer fixed-length transport result: FAIL (failures=$ASSET_TRANSPORT_FAILURES timeouts=$ASSET_TRANSPORT_TIMEOUTS incomplete=$ASSET_TRANSPORT_INCOMPLETE headerErrors=$ASSET_TRANSPORT_HEADER_ERRORS)"
fi
log "Static asset observed failures/timeouts/incomplete/header-errors/slow: $ASSET_TRANSPORT_FAILURES / $ASSET_TRANSPORT_TIMEOUTS / $ASSET_TRANSPORT_INCOMPLETE / $ASSET_TRANSPORT_HEADER_ERRORS / $ASSET_TRANSPORT_SLOW"

log ""
if [[ "$TRANSPORT_HEADER_FAILURES" -eq 0 && "$TRANSPORT_REQUEST_FAILURES" -eq 0 ]]; then
  log "Shared-cache direct-buffer fixed-length state/stats transport result: PASS"
else
  log "Shared-cache direct-buffer fixed-length state/stats transport result: FAIL (requestFailures=$TRANSPORT_REQUEST_FAILURES headerErrors=$TRANSPORT_HEADER_FAILURES)"
fi
log "Shared-cache direct-buffer fixed-length transport observed failures/timeouts/slow-events: $TRANSPORT_REQUEST_FAILURES / $TRANSPORT_TIMEOUTS / $TRANSPORT_SLOW_EVENTS"

log ""
log "Phase 2/3/4 async load failures: $PHASE2_ASYNC_FAILURES / $PHASE3_ASYNC_FAILURES / $PHASE4_ASYNC_FAILURES"
log "Phase 5 cold/warm/diary load failures: $PHASE5A_ASYNC_FAILURES / $PHASE5B_ASYNC_FAILURES / $PHASE5C_ASYNC_FAILURES"
log "Phase 6 recovery failures: $PHASE6_ASYNC_FAILURES"
if [[ "$PHASE2_ASYNC_FAILURES" -eq 0 && "$PHASE3_ASYNC_FAILURES" -eq 0 && "$PHASE4_ASYNC_FAILURES" -eq 0 && "$PHASE5A_ASYNC_FAILURES" -eq 0 && "$PHASE5B_ASYNC_FAILURES" -eq 0 && "$PHASE5C_ASYNC_FAILURES" -eq 0 && "$PHASE6_ASYNC_FAILURES" -eq 0 ]]; then
  log "ASYNC LOAD REGRESSION RESULT: PASS"
else
  log "ASYNC LOAD REGRESSION RESULT: FAIL"
fi

log ""
log "===== HTTPUPDATE OTA SOURCE CONTRACT ====="
TEST_REPO_ROOT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd || true)"
if [[ -n "$TEST_REPO_ROOT" && -f "$TEST_REPO_ROOT/src/ota.cpp" ]]; then
  if grep -q '#include <HTTPUpdate.h>' "$TEST_REPO_ROOT/src/ota.cpp" && \
     grep -q 'HTTPUpdate otaUpdater' "$TEST_REPO_ROOT/src/ota.cpp" && \
     grep -q 'setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)' "$TEST_REPO_ROOT/src/ota.cpp" && \
     grep -q 'otaUpdater.update(client, firmwareUrl)' "$TEST_REPO_ROOT/src/ota.cpp" && \
     ! grep -q 'esp_https_ota_begin' "$TEST_REPO_ROOT/src/ota.cpp"; then
    log "PASS v0.2.46-style HTTPUpdate transport is present inside the Async OTA worker"
  else
    log "FAIL HTTPUpdate OTA source contract"
    PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  fi
else
  log "INFO source-contract check skipped (repository source not available)"
fi

# v0.4.20 dashboard credential-output hardening. Secret fields must be empty in
# delivered HTML; the Flood webhook token is intentionally exempt.
phase32_secret_html="${OUTDIR}/phase32-dashboard-secret-output.html"
phase32_secret_meta="$(curl -sS -o "$phase32_secret_html" --max-time 10 \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/" 2>>"$LOGFILE")"; phase32_secret_rc=$?
record_result "phase32" "dashboard-secret-output" "$phase32_secret_meta" "$phase32_secret_rc"
IFS=$'\t' read -r phase32_secret_http _ _ <<< "$phase32_secret_meta"
if [[ "$phase32_secret_rc" -eq 0 && "$phase32_secret_http" == "200" ]] &&
   grep -Eq 'id="webPushoverUserKey"[^>]*value=""' "$phase32_secret_html" &&
   grep -Eq 'id="webPushoverAppKey"[^>]*value=""' "$phase32_secret_html" &&
   grep -Eq 'id="webGotifyToken"[^>]*value=""' "$phase32_secret_html" &&
   grep -Eq 'id="shellyPassword"[^>]*value=""' "$phase32_secret_html"; then
  log "PASS dashboard does not return Pushover/Gotify/Shelly secrets"
else
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  log "FAIL dashboard secret fields are not write-only"
fi


# v0.4.22 Operating Settings layout contract. This is a static asset check only;
# it verifies the compact responsive structure without changing device state.
if [[ "$phase32_html" == *'class="page card runsettings-page"'* && \
      "$phase32_html" == *'class="runsettings-top-grid"'* && \
      "$phase32_html" == *'class="runsettings-param-grid"'* && \
      "$phase32_html" == *'class="runsettings-relay-table'* && \
      "$phase32_html" == *'id="irrigationParameterSection"'* ]]; then
  log "PASS v0.4.22 compact Operating Settings layout contract"
else
  log "FAIL v0.4.22 compact Operating Settings layout contract"
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
fi

# v0.4.22 page-aware polling contract. Only Status and System Log may own
# periodic browser polling; Shelly/settings pages must not start background timers.
phase32_polling_js="${OUTDIR}/phase32-page-aware-polling.js"
phase32_polling_meta="$(curl -sS -o "$phase32_polling_js" --max-time 10 \
  -w '%{http_code}\t%{time_total}\t%{size_download}' "$BASE8080/script.js?v=${TEST_CACHE_BUSTER}&page-aware-polling=1" 2>>"$LOGFILE")"; phase32_polling_rc=$?
record_result "phase32" "page-aware-polling" "$phase32_polling_meta" "$phase32_polling_rc"
IFS=$'\t' read -r phase32_polling_http _ _ <<< "$phase32_polling_meta"
if [[ "$phase32_polling_rc" -eq 0 && "$phase32_polling_http" == "200" ]] &&
   grep -q "activeId === 'status'" "$phase32_polling_js" &&
   grep -q "activeId === 'logging'" "$phase32_polling_js" &&
   grep -q "window._stopSensorPoll" "$phase32_polling_js" &&
   ! grep -q "_shellyLightTimer" "$phase32_polling_js"; then
  log "PASS page-aware polling is limited to Status/System Log and Shelly timer is absent"
else
  PHASE32_FINAL_WEB_FAILURES=$((PHASE32_FINAL_WEB_FAILURES + 1))
  log "FAIL page-aware polling contract"
fi

PORT8080_REGRESSION_FAILURES=$((PHASE2_ASYNC_FAILURES + PHASE3_ASYNC_FAILURES + PHASE4_ASYNC_FAILURES + PHASE5A_ASYNC_FAILURES + PHASE5B_ASYNC_FAILURES + PHASE5C_ASYNC_FAILURES + PHASE6_ASYNC_FAILURES + ASSET_FAILURES + CACHE_FAILURES + READCACHE_FAILURES + LOGCLEAR_FAILURES + PREFWRITE_FAILURES + RUNTIME_GROW_FAILURES + RUNTIME_HEAT_FAILURES + RUNTIME_HEAT_OUTPUT_FAILURES + RUNTIME_LIGHT_FAILURES + RUNTIME_RELAY_SCHEDULE_FAILURES + RUNTIME_RELAY_ACTION_FAILURES + RUNTIME_WATERING_FAILURES + RUNTIME_STORAGE_FAILURES + PHASE26_SETTINGS_FAILURES + PHASE27_SHELLY_FAILURES + PHASE28_SHELLY_SETTINGS_FAILURES + PHASE29_SYSTEM_SETTINGS_FAILURES + PHASE30_FACTORY_RESET_FAILURES + PHASE31_WIFI_PROVISION_FAILURES + PHASE32_FINAL_WEB_FAILURES + ASSET_TRANSPORT_FAILURES + ASSET_TRANSPORT_TIMEOUTS + ASSET_TRANSPORT_INCOMPLETE + ASSET_TRANSPORT_HEADER_ERRORS + TRANSPORT_HEADER_FAILURES + TRANSPORT_REQUEST_FAILURES))
if [[ "$PORT8080_REGRESSION_FAILURES" -eq 0 ]]; then
  log "OVERALL PORT-8080 REGRESSION RESULT: PASS"
else
  log "OVERALL PORT-8080 REGRESSION RESULT: FAIL ($PORT8080_REGRESSION_FAILURES problem(s); see component results above)"
fi

RELEASE_GATE_FAILURES=$((PORT8080_REGRESSION_FAILURES + LEGACY_UI_FAILURES))

if [[ "$RELEASE_GATE_FAILURES" -eq 0 ]]; then
  log "OVERALL $EXPECTED_FW_VERSION RELEASE GATE: PASS"
else
  log "OVERALL $EXPECTED_FW_VERSION RELEASE GATE: FAIL ($RELEASE_GATE_FAILURES problem(s))"
fi

log ""
log "Finished: $(now)"
log "Raw request data: $DATAFILE"
log "Summary: $SUMMARYFILE"
log "Full log: $LOGFILE"

BUNDLE="growtent-async-test-${STAMP}.tar.gz"
if command -v tar >/dev/null 2>&1; then
  tar -czf "$BUNDLE" "$OUTDIR"
  log "Upload bundle for analysis: $BUNDLE"
else
  log "tar not found; upload the .txt and requests.tsv files from $OUTDIR instead."
fi

# v0.4.17 HTTPUpdate transport source guards
grep -q '#include <HTTPUpdate.h>' "$PROJECT_DIR/src/ota.cpp" || fail "HTTPUpdate include missing"
grep -q 'client.setInsecure();' "$PROJECT_DIR/src/ota.cpp" || fail "v0.2.46 WiFiClientSecure transport setup missing"
grep -q 'otaUpdater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);' "$PROJECT_DIR/src/ota.cpp" || fail "HTTPUpdate redirect handling missing"
grep -q 'g_otaUpdateStreaming = true;' "$PROJECT_DIR/src/ota.cpp" || fail "OTA progress streaming flag missing"


# v0.4.17 exclusive OTA maintenance source contract
TEST_REPO_ROOT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd || true)"
if [[ -n "$TEST_REPO_ROOT" && -f "$TEST_REPO_ROOT/src/ota.cpp" ]]; then
  grep -q 'OTA_EXCLUSIVE_QUIESCE_TIMEOUT_MS = 20000UL' "$TEST_REPO_ROOT/src/ota.cpp" || fail "exclusive OTA quiesce timeout missing"
  grep -q 'otaRequestExclusiveMaintenance' "$TEST_REPO_ROOT/src/ota.cpp" || fail "exclusive OTA request missing"
  grep -q 'otaMaintenanceAllTasksStopped' "$TEST_REPO_ROOT/src/ota.cpp" || fail "exclusive OTA task barrier missing"
  if grep -q 'asyncServer.end();' "$TEST_REPO_ROOT/src/main.cpp"; then fail "exclusive OTA must retain TCP/8080 for live progress"; fi
grep -q 'ota_in_progress' "$TEST_REPO_ROOT/src/main.cpp" || fail "OTA maintenance state/stats guard missing"
  grep -q 'LittleFS.end();' "$TEST_REPO_ROOT/src/main.cpp" || fail "exclusive OTA LittleFS shutdown missing"
  grep -q 'preferences.end();' "$TEST_REPO_ROOT/src/main.cpp" || fail "exclusive OTA Preferences shutdown missing"
  grep -q 'Wire.end();' "$TEST_REPO_ROOT/src/main.cpp" || fail "exclusive OTA I2C shutdown missing"
fi


# v0.4.17 non-flashing OTA transport diagnostic source contract
TEST_REPO_ROOT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd || true)"
if [[ -n "$TEST_REPO_ROOT" && -f "$TEST_REPO_ROOT/src/ota.cpp" && -f "$TEST_REPO_ROOT/src/main.cpp" ]]; then
  grep -q 'runOtaTransportTest' "$TEST_REPO_ROOT/src/ota.cpp" || fail "OTA transport diagnostic worker missing"
  grep -q 'otaTransportTestStatusJson' "$TEST_REPO_ROOT/src/ota.cpp" || fail "OTA transport diagnostic status missing"
  grep -q '/api/ota/transport-test' "$TEST_REPO_ROOT/src/main.cpp" || fail "OTA transport diagnostic route missing"
  grep -q 'signed URL hidden' "$TEST_REPO_ROOT/src/ota.cpp" || fail "signed release URL privacy guard missing"
  grep -q 'GET is intentional' "$TEST_REPO_ROOT/src/ota.cpp" || fail "non-flashing GET-header probe contract missing"
  [[ -x "$TEST_REPO_ROOT/tests/test_ota_transport.sh" ]] || fail "test_ota_transport.sh missing or not executable"
fi


# v0.4.17 HTTPUpdate preflight diagnostic source contract
TEST_REPO_ROOT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd || true)"
if [[ -n "$TEST_REPO_ROOT" && -f "$TEST_REPO_ROOT/src/ota.cpp" ]]; then
  grep -q 'otaRunHttpUpdatePreflight' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate preflight diagnostic missing"
  grep -q 'ESP.getSketchMD5()' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate MD5 preflight missing"
  grep -q 'esp_partition_get_sha256' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate SHA256 preflight missing"
  grep -q 'http.setUserAgent("ESP32-http-Update")' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate preflight user-agent mismatch"
  grep -q 'http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate preflight redirect mode mismatch"
  grep -q 'http.setTimeout(8000)' "$TEST_REPO_ROOT/src/ota.cpp" || fail "HTTPUpdate preflight timeout mismatch"
fi


# v0.4.17 cold-first HTTPUpdate preflight diagnostic source contract
TEST_REPO_ROOT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd || true)"
if [[ -n "$TEST_REPO_ROOT" && -f "$TEST_REPO_ROOT/src/ota.cpp" ]]; then
  grep -q 'cold_httpupdate_preflight' "$TEST_REPO_ROOT/src/ota.cpp" || fail "cold-first HTTPUpdate preflight stage missing"
  grep -q 'COLD HTTPUPDATE PREFLIGHT' "$TEST_REPO_ROOT/src/ota.cpp" || fail "cold-first HTTPUpdate preflight diagnostic label missing"
  grep -q 'otaStorePreflightResult(cold, diag, true)' "$TEST_REPO_ROOT/src/ota.cpp" || fail "cold-first HTTPUpdate result capture missing"
fi
