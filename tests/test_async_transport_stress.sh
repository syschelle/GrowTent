#!/usr/bin/env bash
set -u
set -o pipefail

# GrowTent focused Async transport stress diagnostic
#
# Read-only test. It does NOT change settings, relays, watering, Shellys,
# LittleFS contents, OTA state, or reboot the ESP.
#
# Default load:
#   A) 30 rounds x (4 /api/state + 4 /api/stats) simultaneous requests
#   B) 40 rounds x (4 /api/diary/list + 4 /api/diary/csv) simultaneous requests
#
# Usage:
#   ./test_async_transport_stress.sh 192.168.178.58
#
# Optional environment overrides:
#   PORT=8080
#   BURST_ROUNDS=30
#   DIARY_ROUNDS=40
#   PARALLEL_PER_ENDPOINT=4
#   MAX_TIME=6
#   CONNECT_TIMEOUT=3
#   ROUND_PAUSE=0.20
#
# The script intentionally does not retry failed requests. A retry would hide
# the exact curl rc=18 / rc=28 conditions we are trying to reproduce.

ESP_IP="${1:-}"
if [[ -z "$ESP_IP" ]]; then
  echo "Usage: $0 <ESP-IP>"
  exit 2
fi

PORT="${PORT:-8080}"
BURST_ROUNDS="${BURST_ROUNDS:-30}"
DIARY_ROUNDS="${DIARY_ROUNDS:-40}"
PARALLEL_PER_ENDPOINT="${PARALLEL_PER_ENDPOINT:-4}"
MAX_TIME="${MAX_TIME:-6}"
CONNECT_TIMEOUT="${CONNECT_TIMEOUT:-3}"
ROUND_PAUSE="${ROUND_PAUSE:-0.20}"

BASE="http://${ESP_IP}:${PORT}"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
OUTDIR="growtent-async-transport-stress-${STAMP}"
LOGFILE="${OUTDIR}/transport-stress-${STAMP}.txt"
RESULTS_TSV="${OUTDIR}/results.tsv"
FAILURES_TSV="${OUTDIR}/failures.tsv"

mkdir -p "$OUTDIR"/{burst,diary,health}
: > "$LOGFILE"
: > "$FAILURES_TSV"

printf 'phase\tround\tslot\tendpoint\thttp\tcurl_rc\ttotal_s\treported_bytes\tactual_bytes\tcontent_length\tcontent_type\ttransfer_encoding\tstate_transport\tstate_source_length\tfirst_byte\tjson_ok\tfailure_reason\n' > "$RESULTS_TSV"

log() {
  printf '%s\n' "$*" | tee -a "$LOGFILE"
}

have_jq=false
if command -v jq >/dev/null 2>&1; then
  have_jq=true
fi

extract_header() {
  local headers="$1" name="$2"
  awk -v want="$name" '
    BEGIN { IGNORECASE=1 }
    {
      key=$1
      sub(/:$/, "", key)
      if (tolower(key)==tolower(want)) {
        sub(/^[^:]*:[[:space:]]*/, "")
        sub(/\r$/, "")
        value=$0
      }
    }
    END { print value }
  ' "$headers" 2>/dev/null || true
}

last_http_status() {
  local headers="$1"
  awk '
    toupper($1) ~ /^HTTP\// { code=$2 }
    END { print code }
  ' "$headers" 2>/dev/null || true
}

json_valid() {
  local body="$1"
  if [[ "$have_jq" == true ]]; then
    jq -e . "$body" >/dev/null 2>&1
    return $?
  fi

  # Fallback only checks that the payload looks JSON-like. jq is recommended.
  local first last
  first="$(head -c 1 "$body" 2>/dev/null || true)"
  last="$(tail -c 1 "$body" 2>/dev/null || true)"
  [[ "$first" == "{" || "$first" == "[" ]] && [[ "$last" == "}" || "$last" == "]" ]]
}

csv_valid() {
  local body="$1"
  [[ -s "$body" ]]
}

request_one() {
  local phase="$1" round="$2" slot="$3" endpoint="$4" kind="$5" dir="$6"
  local safe_endpoint
  safe_endpoint="$(printf '%s' "$endpoint" | tr '/?' '__' | tr -cd '[:alnum:]_.-')"

  local prefix="${dir}/r$(printf '%03d' "$round")-${slot}-${safe_endpoint}"
  local body="${prefix}.body"
  local headers="${prefix}.headers"
  local stderr="${prefix}.stderr"
  local meta="${prefix}.meta"
  local result="${prefix}.result"

  local curl_meta rc http total reported actual content_length content_type transfer
  local state_transport source_length first_byte json_ok failure_reason

  curl_meta="$(
    curl -sS --http1.1 \
      --connect-timeout "$CONNECT_TIMEOUT" \
      --max-time "$MAX_TIME" \
      -D "$headers" \
      -o "$body" \
      -w '%{http_code}\t%{time_total}\t%{size_download}' \
      "${BASE}${endpoint}" \
      2>"$stderr"
  )"
  rc=$?

  printf '%s\n' "$curl_meta" > "$meta"

  IFS=$'\t' read -r http total reported <<< "$curl_meta"
  http="${http:-000}"
  total="${total:-0}"
  reported="${reported:-0}"

  if [[ -f "$body" ]]; then
    actual="$(wc -c < "$body" | tr -d '[:space:]')"
    first_byte="$(head -c 1 "$body" 2>/dev/null || true)"
  else
    actual=0
    first_byte=""
  fi

  content_length="$(extract_header "$headers" "Content-Length")"
  content_type="$(extract_header "$headers" "Content-Type")"
  transfer="$(extract_header "$headers" "Transfer-Encoding")"
  state_transport="$(extract_header "$headers" "X-GrowTent-State-Transport")"
  source_length="$(extract_header "$headers" "X-GrowTent-State-Source-Length")"

  json_ok="na"
  if [[ "$kind" == "json" ]]; then
    if json_valid "$body"; then
      json_ok="true"
    else
      json_ok="false"
    fi
  elif [[ "$kind" == "csv" ]]; then
    if csv_valid "$body"; then
      json_ok="true"
    else
      json_ok="false"
    fi
  fi

  failure_reason=""

  if [[ "$rc" -ne 0 ]]; then
    failure_reason="curl_rc_${rc}"
  elif [[ "$http" != "200" ]]; then
    failure_reason="http_${http}"
  elif [[ -n "$content_length" && "$content_length" =~ ^[0-9]+$ && "$actual" -ne "$content_length" ]]; then
    failure_reason="content_length_mismatch"
  elif [[ "$reported" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    # curl size_download can be decimal-looking on some builds; compare integer part.
    local reported_int="${reported%%.*}"
    if [[ "$reported_int" -ne "$actual" ]]; then
      failure_reason="reported_size_mismatch"
    fi
  fi

  if [[ -z "$failure_reason" && "$kind" == "json" && "$json_ok" != "true" ]]; then
    failure_reason="invalid_json"
  fi
  if [[ -z "$failure_reason" && "$kind" == "csv" && "$json_ok" != "true" ]]; then
    failure_reason="empty_csv"
  fi

  # For /api/state and /api/stats, a fixed-length response is required.
  if [[ -z "$failure_reason" && ( "$endpoint" == "/api/state" || "$endpoint" == "/api/stats" ) ]]; then
    if [[ -n "$transfer" ]]; then
      failure_reason="unexpected_transfer_encoding"
    elif [[ -z "$content_length" ]]; then
      failure_reason="missing_content_length"
    fi
  fi

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$phase" "$round" "$slot" "$endpoint" "$http" "$rc" "$total" "$reported" "$actual" \
    "${content_length:-}" "${content_type:-}" "${transfer:-}" "${state_transport:-}" \
    "${source_length:-}" "${first_byte:-}" "$json_ok" "${failure_reason:-}" > "$result"

  if [[ -n "$failure_reason" ]]; then
    return 1
  fi
  return 0
}

collect_round() {
  local phase="$1" round="$2" dir="$3"
  shift 3
  local specs=("$@")

  local failures=0
  local result line reason
  for result in "${dir}/r$(printf '%03d' "$round")-"*.result; do
    [[ -e "$result" ]] || continue
    line="$(cat "$result")"
    printf '%s\n' "$line" >> "$RESULTS_TSV"
    reason="$(printf '%s\n' "$line" | awk -F'\t' '{print $17}')"
    if [[ -n "$reason" ]]; then
      failures=$((failures + 1))
      printf '%s\n' "$line" >> "$FAILURES_TSV"
    else
      rm -f "${result%.result}.body" "${result%.result}.headers" \
            "${result%.result}.stderr" "${result%.result}.meta" "$result"
    fi
  done

  return "$failures"
}

health_snapshot() {
  local label="$1"
  local file="${OUTDIR}/health/${label}.json"
  local headers="${OUTDIR}/health/${label}.headers"
  local rc=0
  curl -sS --http1.1 --connect-timeout "$CONNECT_TIMEOUT" --max-time "$MAX_TIME" \
    -D "$headers" -o "$file" "${BASE}/api/async/health" || rc=$?
  if [[ "$rc" -eq 0 && -s "$file" ]]; then
    log "Health ${label}: $(tr -d '\n' < "$file")"
  else
    log "WARN health ${label} failed rc=$rc"
  fi
}

print_failure_details() {
  local phase="$1" round="$2" slot="$3" endpoint="$4" http="$5" rc="$6" total="$7"
  local reported="$8" actual="$9" cl="${10}" reason="${17}"

  log "FAIL ${phase} round=${round} slot=${slot} endpoint=${endpoint} HTTP=${http} rc=${rc} total=${total}s reportedBytes=${reported} actualBytes=${actual} contentLength='${cl:-missing}' reason=${reason}"
}

run_burst_phase() {
  local failures=0 total_requests=0
  log ""
  log "===== PHASE A: STATE/STATS BURST ====="
  log "Load: ${BURST_ROUNDS} rounds x (${PARALLEL_PER_ENDPOINT} state + ${PARALLEL_PER_ENDPOINT} stats) simultaneous requests; timeout=${MAX_TIME}s"
  log "No retries. Any curl rc=18/28 or length mismatch is preserved."

  for ((round=1; round<=BURST_ROUNDS; round++)); do
    local pids=()
    local slot=0

    for ((i=1; i<=PARALLEL_PER_ENDPOINT; i++)); do
      slot=$((slot + 1))
      request_one "burst" "$round" "state-${i}" "/api/state" "json" "${OUTDIR}/burst" &
      pids+=("$!")
      slot=$((slot + 1))
      request_one "burst" "$round" "stats-${i}" "/api/stats" "json" "${OUTDIR}/burst" &
      pids+=("$!")
    done

    local round_fail=0
    for pid in "${pids[@]}"; do
      if ! wait "$pid"; then
        round_fail=$((round_fail + 1))
      fi
    done

    total_requests=$((total_requests + 2 * PARALLEL_PER_ENDPOINT))

    # Append results and keep artifacts only for failures.
    local result line reason
    for result in "${OUTDIR}/burst/r$(printf '%03d' "$round")-"*.result; do
      [[ -e "$result" ]] || continue
      line="$(cat "$result")"
      printf '%s\n' "$line" >> "$RESULTS_TSV"
      reason="$(printf '%s\n' "$line" | awk -F'\t' '{print $17}')"
      if [[ -n "$reason" ]]; then
        failures=$((failures + 1))
        printf '%s\n' "$line" >> "$FAILURES_TSV"
        IFS=$'\t' read -r p r s e h c t rb ab cl ct te st sl fb jo fr <<< "$line"
        print_failure_details "$p" "$r" "$s" "$e" "$h" "$c" "$t" "$rb" "$ab" "$cl" "$ct" "$te" "$st" "$sl" "$fb" "$jo" "$fr"
      else
        rm -f "${result%.result}.body" "${result%.result}.headers" \
              "${result%.result}.stderr" "${result%.result}.meta"
      fi
      rm -f "$result"
    done

    log "Burst progress: ${round}/${BURST_ROUNDS}; cumulative failures=${failures}"
    sleep "$ROUND_PAUSE"
  done

  log "PHASE A RESULT: requests=${total_requests} failures=${failures}"
  PHASE_A_FAILURES="$failures"
}

run_diary_phase() {
  local failures=0 total_requests=0
  log ""
  log "===== PHASE B: DIARY READ BURST ====="
  log "Load: ${DIARY_ROUNDS} rounds x (${PARALLEL_PER_ENDPOINT} diary/list + ${PARALLEL_PER_ENDPOINT} diary/csv) simultaneous requests; timeout=${MAX_TIME}s"
  log "Read-only. Any timeout or incomplete response is preserved."

  for ((round=1; round<=DIARY_ROUNDS; round++)); do
    local pids=()

    for ((i=1; i<=PARALLEL_PER_ENDPOINT; i++)); do
      request_one "diary" "$round" "list-${i}" "/api/diary/list" "json" "${OUTDIR}/diary" &
      pids+=("$!")
      request_one "diary" "$round" "csv-${i}" "/api/diary/csv" "csv" "${OUTDIR}/diary" &
      pids+=("$!")
    done

    for pid in "${pids[@]}"; do
      wait "$pid" || true
    done

    total_requests=$((total_requests + 2 * PARALLEL_PER_ENDPOINT))

    local result line reason
    for result in "${OUTDIR}/diary/r$(printf '%03d' "$round")-"*.result; do
      [[ -e "$result" ]] || continue
      line="$(cat "$result")"
      printf '%s\n' "$line" >> "$RESULTS_TSV"
      reason="$(printf '%s\n' "$line" | awk -F'\t' '{print $17}')"
      if [[ -n "$reason" ]]; then
        failures=$((failures + 1))
        printf '%s\n' "$line" >> "$FAILURES_TSV"
        IFS=$'\t' read -r p r s e h c t rb ab cl ct te st sl fb jo fr <<< "$line"
        print_failure_details "$p" "$r" "$s" "$e" "$h" "$c" "$t" "$rb" "$ab" "$cl" "$ct" "$te" "$st" "$sl" "$fb" "$jo" "$fr"
      else
        rm -f "${result%.result}.body" "${result%.result}.headers" \
              "${result%.result}.stderr" "${result%.result}.meta"
      fi
      rm -f "$result"
    done

    log "Diary progress: ${round}/${DIARY_ROUNDS}; cumulative failures=${failures}"
    sleep "$ROUND_PAUSE"
  done

  log "PHASE B RESULT: requests=${total_requests} failures=${failures}"
  PHASE_B_FAILURES="$failures"
}

log "GrowTent focused Async transport stress diagnostic"
log "Started: $(date '+%Y-%m-%d %H:%M:%S')"
log "ESP: ${ESP_IP}"
log "Base: ${BASE}"
log "Output: ${OUTDIR}"
log "jq available: ${have_jq}"
log "Parameters: BURST_ROUNDS=${BURST_ROUNDS} DIARY_ROUNDS=${DIARY_ROUNDS} PARALLEL_PER_ENDPOINT=${PARALLEL_PER_ENDPOINT} MAX_TIME=${MAX_TIME}s CONNECT_TIMEOUT=${CONNECT_TIMEOUT}s"
log "READ-ONLY TEST: no settings, relay, watering, Shelly, LittleFS write, OTA, or reboot action."

# Basic compatibility / reachability check
preflight_body="${OUTDIR}/health/preflight.json"
preflight_headers="${OUTDIR}/health/preflight.headers"
preflight_meta="$(
  curl -sS --http1.1 --connect-timeout "$CONNECT_TIMEOUT" --max-time "$MAX_TIME" \
    -D "$preflight_headers" -o "$preflight_body" \
    -w '%{http_code}\t%{time_total}\t%{size_download}' \
    "${BASE}/api/async/health" 2>>"$LOGFILE"
)"
preflight_rc=$?
IFS=$'\t' read -r pre_http pre_time pre_bytes <<< "$preflight_meta"
if [[ "$preflight_rc" -ne 0 || "$pre_http" != "200" ]]; then
  log "ABORT preflight failed: HTTP=${pre_http:-000} rc=${preflight_rc} time=${pre_time:-0}s"
  exit 2
fi

if [[ "$have_jq" == true ]]; then
  fw="$(jq -r '.version // "unknown"' "$preflight_body")"
  phase="$(jq -r '.phase // "unknown"' "$preflight_body")"
  log "Preflight: PASS firmware=${fw} phase=${phase} HTTP=${pre_http}"
else
  log "Preflight: PASS HTTP=${pre_http} (install jq for JSON field summary)"
fi

health_snapshot "before"

PHASE_A_FAILURES=0
PHASE_B_FAILURES=0

run_burst_phase
health_snapshot "after-burst"

run_diary_phase
health_snapshot "after-diary"

log ""
log "===== SUMMARY ====="
log "Phase A state/stats failures: ${PHASE_A_FAILURES}"
log "Phase B diary failures:       ${PHASE_B_FAILURES}"
log "Failure table: ${FAILURES_TSV}"
log "All results:   ${RESULTS_TSV}"

if [[ "$PHASE_A_FAILURES" -eq 0 && "$PHASE_B_FAILURES" -eq 0 ]]; then
  log "FOCUSED ASYNC TRANSPORT STRESS RESULT: PASS"
  exit 0
else
  log "FOCUSED ASYNC TRANSPORT STRESS RESULT: FAIL"
  log "Failure artifacts were preserved under ${OUTDIR}/burst and ${OUTDIR}/diary."
  exit 1
fi
