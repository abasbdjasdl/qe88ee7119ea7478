
# R16 diagnostic startup hook. The original early boot tasks run first.
r16_early_status=$?
(
    /bin/sleep 30
    /bin/sh /usr/libexec/r16-autolog.sh
) > /dev/console 2>&1 &
exit "$r16_early_status"
