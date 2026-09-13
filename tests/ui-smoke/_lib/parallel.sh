#!/bin/bash
# Sharing the machine with other tests.
#
# Two things break when several ui-smoke tests run at once, and both come
# from the same place: the machine no longer belongs to one test.
#
#   Identity. The GUI process names are the same in every session -- two
#   tests can run bin/touchy or qtvcp at the same time -- so pgrep alone
#   hands back another test's GUI. Signalling that one makes the other
#   test fail, and then both of them look flaky. A process is ours when
#   its LINUXCNC_INSTANCE matches ours; see src/rtapi/rtapi_instance.h.
#
#   Patience. Every wait here is a wall clock timeout sized for a GUI
#   that has the machine to itself. With N sessions starting, homing and
#   shutting down together, each one is slower. runtests sets
#   LINUXCNC_TEST_TIMEOUT_SCALE to its job count and the waits stretch by
#   that much.
#
# Source with no state needed; both halves work unchanged when nothing is
# set, which is the single-session case.

# Instance a pid belongs to.  A process with no LINUXCNC_INSTANCE counts
# as instance 0, as does an unreadable one -- we cannot signal another
# user's process anyway.
instance_of_pid() {
    inst=$(tr '\0' '\n' < "/proc/$1/environ" 2>/dev/null \
        | sed -n 's/^LINUXCNC_INSTANCE=//p')
    echo "${inst:-0}"
}

# pgrep -f, narrowed to this instance.  Prints one pid per line.
instance_pgrep() {
    for p in $(pgrep -f "$1" 2>/dev/null); do
        if [ "$(instance_of_pid "$p")" = "${LINUXCNC_INSTANCE:-0}" ]; then
            echo "$p"
        fi
    done
}

# pgrep -x, narrowed to this instance.  Prints one pid per line.
instance_pgrep_exact() {
    for p in $(pgrep -x "$1" 2>/dev/null); do
        if [ "$(instance_of_pid "$p")" = "${LINUXCNC_INSTANCE:-0}" ]; then
            echo "$p"
        fi
    done
}

# The one python process of this instance whose command line matches.
# pgrep -f matches whole command lines, so the wrappers around the GUI
# (the linuxcnc launcher, the xvfb-run shell, the launcher's bash -c)
# match too -- the GUI name appears in the config path or the script
# text. Every wrapper has a shell or xvfb-run as argv[0]; the GUI itself
# is a python interpreter. Prints nothing if there is no such process.
instance_gui_pid() {
    for p in $(instance_pgrep "$1"); do
        arg0=$(tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | head -1)
        case "$(basename "$arg0" 2>/dev/null)" in
            python*) echo "$p"; return 0 ;;
        esac
    done
    return 1
}

# Seconds to wait, stretched by LINUXCNC_TEST_TIMEOUT_SCALE.  Rounds up
# and never shortens, so an unset or nonsense scale leaves the argument
# untouched.
scaled_seconds() {
    awk -v base="$1" -v scale="${LINUXCNC_TEST_TIMEOUT_SCALE:-1}" 'BEGIN {
        if (scale !~ /^[0-9]+(\.[0-9]+)?$/ || scale < 1) scale = 1
        v = base * scale
        r = int(v)
        if (v > r) r = r + 1
        print r
    }'
}

# What instance_gui_pid had to choose between, one "pid instance argv" line
# per candidate. For a failure message: a GUI that did not react to a signal
# and a GUI that was never signalled look the same from outside, and this
# tells them apart.
instance_pgrep_report() {
    for p in $(pgrep -f "$1" 2>/dev/null); do
        echo "    pid $p instance $(instance_of_pid "$p")" \
             "$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null)"
    done
}

# Kernel-visible state of a process: run/sleep letter, what it is blocked
# in if the kernel will say, and its signal masks. The masks are the ones
# that matter for "it did not react to SIGTERM": SigBlk with bit 15 set
# means the signal is being held, SigIgn means it was thrown away, and
# SigPnd means it arrived and is still waiting to be taken.
instance_pid_state() {
    echo "state $(awk '{print $3}' "/proc/$1/stat" 2>/dev/null)" \
         "wchan $(cat "/proc/$1/wchan" 2>/dev/null)" \
         "$(grep -E '^Sig(Pnd|Blk|Ign|Cgt)' "/proc/$1/status" 2>/dev/null \
            | tr -s '[:space:]' ' ')"
    for t in /proc/"$1"/task/*; do
        [ -d "$t" ] || continue
        echo "    thread ${t##*/} $(cat "$t/comm" 2>/dev/null)" \
             "state $(awk '{print $3}' "$t/stat" 2>/dev/null)" \
             "wchan $(cat "$t/wchan" 2>/dev/null)"
    done
}

# Wait until a GUI has finished starting up and is idle in its event loop.
#
# NML readiness -- what drive.py reports, and all the quit test had to go
# on -- only says the linuxcnc task is up. A GUI can still be halfway
# through building its window seconds later, and on a loaded machine it
# regularly is. That matters for anything that signals the GUI: a GUI
# under construction has whatever handler it arms for startup, not the one
# the test means to exercise, and gmoccapy for one loses a SIGTERM that
# lands in that window (its early handler raises SystemExit from inside a
# GTK callback, where it is discarded at the C boundary).
#
# Construction is CPU bound and an idle event loop is not, by a wide
# margin: while building, the GUI burns 0.4-2 seconds of CPU per second;
# idle in its loop it uses well under a tenth of that. Watching its own
# CPU time works for every GUI here, including the offscreen Qt one that
# has no X window to look for.
#
# Returns 0 once idle, 1 if it never settles -- the caller carries on
# either way, so a GUI that really is wedged still fails its test rather
# than being quietly skipped.
GUI_IDLE_TICKS=${GUI_IDLE_TICKS:-25}     # CPU ticks per second that count as idle
GUI_IDLE_WINDOWS=${GUI_IDLE_WINDOWS:-3}  # consecutive idle seconds wanted

wait_for_gui_ready() {
    ready_pid="$1"
    ready_limit=$(scaled_seconds "${2:-60}")
    ready_prev=""
    ready_idle=0
    ready_waited=0
    while [ "$ready_waited" -lt "$ready_limit" ]; do
        ready_cur=$(awk '{print $14 + $15}' "/proc/$ready_pid/stat" 2>/dev/null)
        if [ -z "$ready_cur" ]; then
            echo "gui-ready: pid $ready_pid is gone"
            return 1
        fi
        if [ -n "$ready_prev" ]; then
            if [ $((ready_cur - ready_prev)) -le "$GUI_IDLE_TICKS" ]; then
                ready_idle=$((ready_idle + 1))
                if [ "$ready_idle" -ge "$GUI_IDLE_WINDOWS" ]; then
                    echo "gui-ready: pid $ready_pid idle after ${ready_waited}s"
                    return 0
                fi
            else
                ready_idle=0
            fi
        fi
        ready_prev="$ready_cur"
        ready_waited=$((ready_waited + 1))
        sleep 1
    done
    echo "gui-ready: pid $ready_pid still busy after ${ready_limit}s, carrying on"
    return 1
}
