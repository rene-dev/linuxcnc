#!/bin/bash

# linuxcncrsh shifts its default port with LINUXCNC_INSTANCE, so that
# parallel test runs do not fight over one socket.
RSHPORT=$((5007 + 16 * ${LINUXCNC_INSTANCE:-0}))


rm -f gcode-output

if nc -z localhost $RSHPORT; then
    echo "Process already listening on port $RSHPORT. Exiting"
    exit 1
fi

linuxcnc -r linuxcncrsh-test.ini &


# let linuxcnc come up
TOGO=80
while [  $TOGO -gt 0 ]; do
    echo "trying to connect to linuxcncrsh TOGO=$TOGO"
    if nc -z localhost $RSHPORT; then
        break
    fi
    sleep 0.25
    TOGO=$((TOGO - 1))
done
if [  $TOGO -eq 0 ]; then
    echo "connection to linuxcncrsh timed out"
    exit 1
fi


(
    echo "hello EMC mt 1.0"
    echo "set enable EMCTOO"

    # ask linuxcncrsh to not read the next command until it's done running
    # the current one
    echo "set set_wait done"

    echo "set mode manual"
    echo "set estop off"
    echo "set machine on"

    echo "set mode mdi"
    echo "set mdi m100 p-1 q-2"
    sleep 1

    # here comes a big blob
    dd bs=4096 if=lots-of-gcode

    echo "set mdi m100 p-3 q-4"

    echo "shutdown"
) | nc localhost $RSHPORT


# wait for linuxcnc to finish
wait

exit 0

