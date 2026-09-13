#!/bin/bash
# Run a shipped sim config from a private copy.
#
# A config directory is not private state, but it is written to: the
# interpreter variable file (PARAMETER_FILE) lives in it, and several of
# these tests run the same config -- touchy, touchy-quit and touchy-fit all
# boot configs/sim/touchy. Run side by side they write one another's
# variable file. Copying the directory first gives each test its own, and
# has the side benefit that a test run leaves nothing behind under configs/.
#
# The same trick the gmoccapy and qtdragon tests already use for their own
# reasons (a read-only workspace on CI); this is the plain version for a
# config that needs no other patching.
#
# Usage, from a test.sh with LIB_DIR set:
#     . "$LIB_DIR/mirror-config.sh"
#     mirror_sim_config touchy/touchy.ini
#     "$LIB_DIR/run-gui.sh" "$MIRROR_INI" ...
#     rc=$?; mirror_cleanup; exit $rc
#
# Do not exec the launcher: the copy has to outlive it and then be removed.

: "${LIB_DIR:?mirror-config.sh must be sourced with LIB_DIR set}"

MIRROR_DIR=""
MIRROR_INI=""

mirror_sim_config() {
    mirror_rel="$1"
    mirror_src="$(cd "$LIB_DIR/../../../configs/sim/$(dirname "$mirror_rel")" && pwd)"
    mirror_base="$(basename "$mirror_rel")"

    MIRROR_DIR="$(mktemp -d -t ui-smoke-config.XXXXXX)"
    cp -r "$mirror_src/." "$MIRROR_DIR/"
    MIRROR_INI="$MIRROR_DIR/$mirror_base"

    # PROGRAM_PREFIX is written relative to the config directory, so from
    # the copy it points nowhere and the file chooser throws on every
    # repopulate. Re-root it at the original location.
    sed -i "s|^\(PROGRAM_PREFIX *= *\)\.\./\.\./|\1$mirror_src/../../|" "$MIRROR_INI"
}

mirror_cleanup() {
    if [ -n "$MIRROR_DIR" ] && [ -d "$MIRROR_DIR" ]; then
        rm -rf "$MIRROR_DIR"
    fi
}
