#!/bin/bash
LIB_DIR="$(cd "$(dirname "$0")/../_lib" && pwd)"
# Private copy of the config; see mirror-config.sh.
. "$LIB_DIR/mirror-config.sh"
mirror_sim_config touchy/touchy.ini
"$LIB_DIR/quit-launch.sh" "$MIRROR_INI" "bin/touchy"
rc=$?
mirror_cleanup
exit $rc
