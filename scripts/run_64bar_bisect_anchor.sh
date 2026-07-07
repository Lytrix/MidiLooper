# PARKED per DEC-017 — do not use; long HITL bisect skipped in favour of runtime redesign.
# Usage: ./scripts/run_64bar_bisect_anchor.sh <git-sha> [label]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SHA="${1:?sha required}"
LABEL="${2:-$SHA}"
PORT="${SERIAL_PORT:-/dev/cu.usbmodem154944801}"
PY="${ROOT}/.venv/bin/python"
RESULT_FILE="${ROOT}/captures/bisect_64bar_results.jsonl"

echo "=== Bisect anchor: $LABEL ($SHA) ==="
git checkout "$SHA"

echo "=== Build teensy41-capture-serial ==="
pio run -e teensy41-capture-serial

echo "=== Upload (press PROGRAM MODE if prompted) ==="
pio run -e teensy41-capture-serial -t upload

sleep 3

echo "=== HITL 64+64 track 5 (second overdub off for speed) ==="
"$PY" scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port "$PORT" \
  --track-number 5 --midi-channel 5 \
  --record-bars 64 --overdub-bars 64 --second-overdub-bars 0 \
  --start-transport --no-fixed-grid-notes \
  --clear-before-record \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000 \
  --serial-heartbeat-timeout-seconds 120 \
  --no-undo-redo-after-overdub-stop

# Latest artifacts
JSON="$(ls -t captures/host_midi_automation_baseline_*.json | head -1)"
STAMP="${JSON##*_}"
STAMP="${STAMP%.json}"
SERIAL="captures/host_midi_automation_serial_${STAMP}.log"

PLAYING_ODUB=0
PERS_OK=false
if [[ -f "$SERIAL" ]]; then
  PLAYING_ODUB="$(rg -c 'ST,[^,]+,PLAYING,OVERDUBBING' "$SERIAL" 2>/dev/null || echo 0)"
  if rg -q 'PERS,result' "$SERIAL" && rg -q ',ok' "$SERIAL"; then PERS_OK=true; fi
fi
VERDICT="FAIL"
if [[ "$PLAYING_ODUB" -ge 1 ]]; then VERDICT="PASS"; fi

echo "=== Result: $VERDICT playing_to_odub=$PLAYING_ODUB pers_ok=$PERS_OK ==="
echo "  json=$JSON"
echo "  serial=$SERIAL"

ENTRY=$(printf '{"sha":"%s","label":"%s","verdict":"%s","playing_to_odub":%s,"pers_ok":%s,"json":"%s","serial":"%s","at":"%s"}\n' \
  "$SHA" "$LABEL" "$VERDICT" "$PLAYING_ODUB" "$PERS_OK" "$JSON" "$SERIAL" "$(date -u +%Y-%m-%dT%H:%M:%SZ)")
echo "$ENTRY" >> "$RESULT_FILE"
echo "$ENTRY"
