#!/usr/bin/env bash
# teach.sh — tiny helpers so the lab scripts NARRATE what they do while they run.
# Source it from a lab script:  source "<repo>/tools/teach.sh"
# Then use: step "section", say "why/what", run cmd..., observe "what to notice",
#           predict "write this down first", finding "put this in your notebook".
# Guidance prints to the student's terminal AS the script runs; colors only on a tty.
_tc(){ if [ -t 1 ]; then printf '\033[%sm' "$1"; fi; }
step()    { _tc '1;37'; printf '\n==== %s ====\n' "$*"; _tc '0'; }      # bold section header
say()     { _tc '36';   printf '»  %s\n' "$*";        _tc '0'; }        # what this step does / why (cyan)
run()     { _tc '2';    printf '   $ %s\n' "$*";      _tc '0'; "$@"; }  # show the command, then run it
observe() { _tc '33';   printf '→  %s\n' "$*";        _tc '0'; }        # what you just saw / what to read (yellow)
predict() { _tc '35';   printf '?  PREDICT FIRST: %s\n' "$*"; _tc '0'; }   # active-learning prompt (magenta)
finding() { _tc '32';   printf '✎  FINDING: %s\n' "$*"; _tc '0'; }      # what to record (green)
note()    { _tc '2';    printf '   %s\n' "$*";        _tc '0'; }        # quiet aside
