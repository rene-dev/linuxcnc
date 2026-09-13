#!/bin/bash
LIB_DIR="$(cd "$(dirname "$0")/../_lib" && pwd)"
# Three tests boot the touchy config; each needs its own copy, because the
# interpreter variable file lives in the config directory. See
# mirror-config.sh.
. "$LIB_DIR/mirror-config.sh"
mirror_sim_config touchy/touchy.ini
"$LIB_DIR/run-gui.sh" "$MIRROR_INI" \
    --run-program "$LIB_DIR/smoke.ngc" --expect-delta-mm 1,1,0
rc=$?
mirror_cleanup
exit $rc
