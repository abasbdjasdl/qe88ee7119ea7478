#!/bin/sh
# Called after the recovery system's original early tasks.
r16_call() (
 "$@" & p=$!
 (sleep 5;kill "$p" 2>/dev/null;sleep 1;kill -KILL "$p" 2>/dev/null) >/dev/null 2>&1 & w=$!
 wait "$p"; e=$?;kill "$w" 2>/dev/null;wait "$w" 2>/dev/null;exit "$e"
)
r16_call /bin/launchctl bootstrap system /System/Library/LaunchDaemons/local.r16.autolog.plist || r16_call /bin/launchctl kickstart system/local.r16.autolog
