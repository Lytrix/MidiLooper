#!/usr/bin/env bash
# Quarantine dirty MidiLooper workspace on a Teensy SD card mounted on the host.
#
# Prefer on-device (no SD removal) — teensy41-capture-serial only:
#   pio run -e teensy41-capture-serial -t upload
#   .venv/bin/python scripts/quarantine_workspace_serial_boot.py --port /dev/cu.usbmodem...
#
# Why rename both current/ and recovery/checkpoints/:
#   Boot loads MidiLooper/current/ when workspace.bin or runtime.bundle.bin exists.
#   If that fails or is missing, firmware restores from the newest folder under
#   MidiLooper/recovery/checkpoints/_YYMMDD_HHMM/ — which keeps the broken 64+64 loop.
#
# Power OFF the Teensy before removing the SD card.
#
# Usage:
#   ./scripts/quarantine_dirty_current_set_sd.sh /Volumes/NO\ NAME
#   ./scripts/quarantine_dirty_current_set_sd.sh   # auto-detect MidiLooper on /Volumes/*

set -euo pipefail

stamp="$(date +%Y%m%d_%H%M%S)"

skip_volume() {
  case "$1" in
    "/Volumes/Macintosh HD"|/Volumes/Backups*|/Volumes/com.apple.TimeMachine*|/Volumes/Data)
      return 0
      ;;
  esac
  return 1
}

find_mount_with_midilooper() {
  local vol
  for vol in /Volumes/*; do
    [[ -d "$vol" ]] || continue
    skip_volume "$vol" && continue
    if [[ -d "$vol/MidiLooper" ]]; then
      printf '%s\n' "$vol"
      return 0
    fi
  done
  return 1
}

quarantine_path() {
  local src="$1"
  local label="$2"
  if [[ ! -e "$src" ]]; then
    echo "skip (missing): $label -> $src"
    return 0
  fi
  local dest="${src}.bad.${stamp}"
  echo "quarantine: $label"
  echo "  $src"
  echo "  -> $dest"
  du -sh "$src" 2>/dev/null || true
  mv "$src" "$dest"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '2,12p' "$0"
  exit 0
fi

if [[ -n "${1:-}" ]]; then
  mount_root="$1"
else
  mount_root="$(find_mount_with_midilooper)" || {
    echo "error: no mounted volume with MidiLooper/ found under /Volumes/*" >&2
    echo "Power off Teensy, remove SD, insert in Mac reader, then run:" >&2
    echo "  $0 /Volumes/<SD_VOLUME_NAME>" >&2
    exit 1
  }
fi

root="$mount_root/MidiLooper"
if [[ ! -d "$root" ]]; then
  echo "error: $root does not exist" >&2
  exit 1
fi

echo "SD mount: $mount_root"
echo "Stamp:    $stamp"
echo

quarantine_path "$root/current" "current workspace"
quarantine_path "$root/recovery/checkpoints" "recovery checkpoints"

legacy="$mount_root/midilooper_state.raw"
if [[ -f "$legacy" ]]; then
  quarantine_path "$legacy" "legacy v5 monolith"
fi

echo
echo "Done. Expected on next boot (serial):"
echo '  [StorageManager] Boot recovery chain exhausted; starting empty.'
echo
echo "Track 2 / slot 1 slot file (if you inspect backup): MidiLooper/current/slots/loop_01_00.bin"
echo "Eject SD safely, reinsert in Teensy, power on."
