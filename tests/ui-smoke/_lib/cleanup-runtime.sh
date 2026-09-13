#!/bin/bash
# Reset linuxcnc-related runtime state so the next ui-smoke test starts
# from a clean environment. Used as both a pre-launch belt-and-braces
# cleanup and as a post-shutdown last-resort if scripts/linuxcnc's own
# SIGTERM trap could not reap everything in time.
#
# SHM_KEYS mirrors SHMEM_BASE_KEY in scripts/runtests. If a ui-smoke
# crash leaks any of these, the next runtests invocation aborts in
# test_shmem(); we must clean the full set.
#
# Everything here is confined to this instance (LINUXCNC_INSTANCE, see
# src/rtapi/rtapi_instance.h): the keys carry the instance offset, and a
# daemon is only killed when its own LINUXCNC_INSTANCE matches ours.
# Otherwise a parallel test run would have its sessions shot down from
# under it.

set -u

INSTANCE=${LINUXCNC_INSTANCE:-0}
OFFSET=$((16 * INSTANCE))

DAEMONS=(linuxcncsvr milltask halui rtapi_app)
SHM_BASE_KEYS=(0x00000064 0x48414c32 0x48484c34 0x90280a48 0x130cf406 0x434c522b)

. "$(dirname "${BASH_SOURCE[0]}")/parallel.sh"

for proc in "${DAEMONS[@]}"; do
    for pid in $(instance_pgrep_exact "$proc"); do
        kill -KILL "$pid" 2>/dev/null || true
    done
done

if [ "$INSTANCE" -eq 0 ]; then
    rm -f /tmp/linuxcnc.lock
else
    rm -f "/tmp/linuxcnc-$INSTANCE.lock"
fi
halrun -U 2>/dev/null || true

for base in "${SHM_BASE_KEYS[@]}"; do
    key=$(printf '0x%08x' $((base + OFFSET)))
    shmid=$(LC_ALL=C ipcs -m | awk -v k="$key" 'tolower($1)==k {print $2}')
    if [ -n "$shmid" ]; then
        ipcrm -m "$shmid" 2>/dev/null || true
    fi
done

exit 0
