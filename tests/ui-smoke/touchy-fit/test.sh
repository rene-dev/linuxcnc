#!/bin/bash
# Regression guard: touchy must fit a 1024x600 panel, the common 7"
# touchscreen size it targets. Boots touchy on that screen and fails
# if its window is larger (see window-fit.sh). Phase 1 only; no
# program is run.
LIB_DIR="$(cd "$(dirname "$0")/../_lib" && pwd)"
export UI_SMOKE_XVFB_SCREEN=1024x600x24
export UI_SMOKE_FIT_CLASS='("touchy" "Touchy")'
# Private copy of the config; see mirror-config.sh.
. "$LIB_DIR/mirror-config.sh"
mirror_sim_config touchy/touchy.ini
"$LIB_DIR/run-gui.sh" "$MIRROR_INI"
rc=$?
mirror_cleanup
exit $rc
