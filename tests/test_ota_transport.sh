#!/usr/bin/env bash
# GrowTent OTA transport diagnostic (non-flashing)
# Firmware target: v0.4.16+
#
# Usage:
#   ./tests/test_ota_transport.sh <ESP-IP>
#   ./tests/test_ota_transport.sh <ESP-IP> --normal
#   ./tests/test_ota_transport.sh <ESP-IP> --exclusive
#   ./tests/test_ota_transport.sh <ESP-IP> --both
#
# Default mode is --both: normal first, then exclusive.
# The exclusive test intentionally reboots the ESP after publishing the result.
# No firmware bytes are written by this diagnostic.

set -u
export LC_ALL=C
export LANG=C

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <ESP-IP> [--normal|--exclusive|--both]"
  exit 2
fi

ESP_IP="$1"
MODE="${2:---both}"
BASE="http://${ESP_IP}:8080"
STAMP="$(date '+%Y-%m-%d_%H-%M-%S')"
OUTDIR="growtent-ota-transport-${STAMP}"
LOGFILE="${OUTDIR}/ota-transport-${STAMP}.txt"

mkdir -p "$OUTDIR"

for cmd in curl python3; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: $cmd is required."
    exit 3
  fi
done

case "$MODE" in
  --normal|--exclusive|--both) ;;
  *)
    echo "ERROR: invalid mode '$MODE'"
    echo "Use --normal, --exclusive or --both"
    exit 2
    ;;
esac

log() {
  printf '%s\n' "$*" | tee -a "$LOGFILE"
}

json_field() {
  local file="$1" expr="$2"
  python3 - "$file" "$expr" <<'PY'
import json, sys
p, expr = sys.argv[1], sys.argv[2]
try:
    d = json.load(open(p, encoding="utf-8"))
except Exception:
    print("")
    raise SystemExit
cur = d
for part in expr.split("."):
    if isinstance(cur, dict):
        cur = cur.get(part)
    else:
        cur = None
        break
if cur is True:
    print("true")
elif cur is False:
    print("false")
elif cur is None:
    print("")
else:
    print(cur)
PY
}

pretty_json() {
  python3 -m json.tool "$1" 2>/dev/null || cat "$1"
}

queue_check() {
  local q="${OUTDIR}/check-queue.json"
  local s="${OUTDIR}/check-status.json"

  log ""
  log "===== OTA CHECK / TARGET CACHE ====="
  if ! curl -sS --max-time 15 -o "$q" "$BASE/api/ota/check"; then
    log "FAIL: could not queue OTA check"
    return 1
  fi

  local id
  id="$(json_field "$q" requestId)"
  if [[ -z "$id" ]]; then
    log "FAIL: OTA check queue response has no requestId"
    pretty_json "$q" | tee -a "$LOGFILE"
    return 1
  fi

  log "Queued OTA check requestId=$id"

  local deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    if curl -sS --max-time 8 -o "$s" "$BASE/api/ota/check-status?requestId=$id"; then
      local state
      state="$(json_field "$s" state)"
      case "$state" in
        done)
          log "OTA check: PASS"
          pretty_json "$s" | tee -a "$LOGFILE"
          return 0
          ;;
        failed)
          log "OTA check: FAIL"
          pretty_json "$s" | tee -a "$LOGFILE"
          return 1
          ;;
      esac
    fi
    sleep 0.5
  done

  log "FAIL: OTA check timed out"
  [[ -f "$s" ]] && pretty_json "$s" | tee -a "$LOGFILE"
  return 1
}

run_transport() {
  local mode="$1"
  local exclusive=0
  [[ "$mode" == "exclusive" ]] && exclusive=1

  local q="${OUTDIR}/${mode}-queue.json"
  local s="${OUTDIR}/${mode}-status.json"

  log ""
  log "===== OTA TRANSPORT TEST: ${mode^^} ====="
  log "This test performs DNS/TLS/GET-header probes only. It does NOT flash firmware."

  local id=""
  local queue_deadline=$((SECONDS + 8))
  while (( SECONDS < queue_deadline )); do
    if curl -sS --max-time 10 -X POST \
        -o "$q" "$BASE/api/ota/transport-test?exclusive=$exclusive"; then
      id="$(json_field "$q" requestId)"
      if [[ -n "$id" ]]; then
        break
      fi
      if [[ "$(json_field "$q" error)" == "ota_busy" ]]; then
        sleep 0.25
        continue
      fi
    fi
    break
  done

  if [[ -z "$id" ]]; then
    log "FAIL: transport test could not be queued"
    [[ -f "$q" ]] && pretty_json "$q" | tee -a "$LOGFILE"
    return 1
  fi

  log "Queued $mode transport test requestId=$id"

  local deadline=$((SECONDS + 180))
  local last_message=""
  while (( SECONDS < deadline )); do
    if curl -sS --max-time 8 -o "$s" "$BASE/api/ota/transport-test-status?requestId=$id"; then
      local state message percent
      state="$(json_field "$s" state)"
      message="$(json_field "$s" message)"
      percent="$(json_field "$s" percent)"

      if [[ "$message" != "$last_message" ]]; then
        log "state=${state:-?} percent=${percent:-?} message=${message:-?}"
        last_message="$message"
      fi

      case "$state" in
        done)
          log ""
          log "$mode transport result: PASS"
          pretty_json "$s" | tee -a "$LOGFILE"

          local pre_code pre_len pre_ms pre_md5 pre_sha
          pre_code="$(json_field "$s" httpUpdatePreflight.httpCode)"
          pre_len="$(json_field "$s" httpUpdatePreflight.contentLength)"
          pre_ms="$(json_field "$s" httpUpdatePreflight.ms)"
          pre_md5="$(json_field "$s" httpUpdatePreflight.md5Chars)"
          pre_sha="$(json_field "$s" httpUpdatePreflight.sha256Chars)"
          log "HTTPUpdate preflight: HTTP=${pre_code:-?} len=${pre_len:-?} ms=${pre_ms:-?} MD5chars=${pre_md5:-?} SHA256chars=${pre_sha:-?}"

          if [[ "$mode" == "exclusive" ]]; then
            local cold_code cold_len cold_ms cold_md5 cold_sha
            cold_code="$(json_field "$s" coldHttpUpdatePreflight.httpCode)"
            cold_len="$(json_field "$s" coldHttpUpdatePreflight.contentLength)"
            cold_ms="$(json_field "$s" coldHttpUpdatePreflight.ms)"
            cold_md5="$(json_field "$s" coldHttpUpdatePreflight.md5Chars)"
            cold_sha="$(json_field "$s" coldHttpUpdatePreflight.sha256Chars)"
            log "COLD HTTPUpdate preflight: HTTP=${cold_code:-?} len=${cold_len:-?} ms=${cold_ms:-?} MD5chars=${cold_md5:-?} SHA256chars=${cold_sha:-?}"
            log "ESP will reboot automatically after the result window."
          fi
          return 0
          ;;
        failed)
          log ""
          log "$mode transport result: FAIL"
          pretty_json "$s" | tee -a "$LOGFILE"
          [[ "$mode" == "exclusive" ]] && log "ESP will reboot automatically after the result window."
          return 1
          ;;
      esac
    else
      # During an exclusive run the device may already be entering its reboot
      # window. Keep polling briefly; a final JSON may still arrive.
      log "status poll temporarily unavailable"
    fi
    sleep 0.5
  done

  log "FAIL: $mode transport test timed out"
  [[ -f "$s" ]] && pretty_json "$s" | tee -a "$LOGFILE"
  return 1
}

log "GrowTent OTA transport diagnostic"
log "ESP: $ESP_IP"
log "Mode: $MODE"
log "Output: $OUTDIR"

if ! curl -sS --max-time 8 -o "${OUTDIR}/health.json" "$BASE/api/async/health"; then
  log "FAIL: ESP dashboard/API is not reachable on :8080"
  exit 4
fi

if ! queue_check; then
  log ""
  log "ABORT: a successful OTA check is required so the ESP uses the exact version-pinned firmware URL."
  exit 5
fi

normal_rc=0
exclusive_rc=0

if [[ "$MODE" == "--normal" || "$MODE" == "--both" ]]; then
  run_transport normal || normal_rc=$?
fi

if [[ "$MODE" == "--exclusive" || "$MODE" == "--both" ]]; then
  run_transport exclusive || exclusive_rc=$?
fi

log ""
log "===== SUMMARY ====="
if [[ "$MODE" == "--normal" || "$MODE" == "--both" ]]; then
  log "normal:    $([[ "$normal_rc" -eq 0 ]] && echo PASS || echo FAIL)"
fi
if [[ "$MODE" == "--exclusive" || "$MODE" == "--both" ]]; then
  log "exclusive: $([[ "$exclusive_rc" -eq 0 ]] && echo PASS || echo FAIL)"
fi
log "Full log: $LOGFILE"

if [[ "$normal_rc" -ne 0 || "$exclusive_rc" -ne 0 ]]; then
  exit 1
fi
exit 0
