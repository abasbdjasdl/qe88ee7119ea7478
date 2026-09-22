#!/bin/sh
# Called after the recovery system's original early tasks.
/bin/launchctl bootstrap system /System/Library/LaunchDaemons/local.r16.autolog.plist || /bin/launchctl kickstart system/local.r16.autolog
