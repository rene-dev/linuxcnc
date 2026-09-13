#!/bin/bash
LIB_DIR="$(cd "$(dirname "$0")/../_lib" && pwd)"
# Run from a private copy of the config: the interpreter variable file
# lives in the config directory, so a shared one is written by every
# session at once. See mirror-config.sh.
. "$LIB_DIR/mirror-config.sh"
mirror_sim_config axis/axis.ini
"$LIB_DIR/run-gui.sh" "$MIRROR_INI" \
    --run-program "$LIB_DIR/smoke.ngc" --expect-delta-mm 1,1,0
rc=$?
mirror_cleanup
exit $rc
