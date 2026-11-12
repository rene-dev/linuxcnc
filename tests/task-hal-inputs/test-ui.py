#!/usr/bin/env python3

# Drive Task through its command input pins (task.*), without halui, and
# check that each one has the same effect halui's pin has.

import linuxcnc
import hal
import time
import sys
import os

program_start = time.time()

def log(msg):
    delta_t = time.time() - program_start
    print("%.3f: %s" % (delta_t, msg))
    sys.stdout.flush()


inputs = [
    'machine.on', 'machine.off', 'estop.reset',
    'mode.manual', 'mode.auto', 'mode.mdi', 'mode.teleop', 'mode.joint',
    'mist.on', 'mist.off', 'flood.on', 'flood.off',
    'program.run', 'program.pause', 'program.resume', 'program.step',
    'program.stop', 'abort',
    'program.optional-stop.on', 'program.optional-stop.off',
    'program.block-delete.on', 'program.block-delete.off',
    'home-all', 'joint.0.home', 'joint.0.unhome',
    'spindle.0.start', 'spindle.0.stop', 'spindle.0.forward',
    'spindle.0.reverse', 'spindle.0.increase', 'spindle.0.decrease',
    'spindle.0.brake-on', 'spindle.0.brake-off',
]

# override inputs: name -> HAL type
overrides = {}
for prefix in ('feed-override', 'rapid-override', 'spindle.0.override', 'max-velocity'):
    overrides[prefix + '.counts'] = hal.Type.SINT
    overrides[prefix + '.scale'] = hal.Type.REAL
    for p in ('count-enable', 'direct-value', 'increase', 'decrease'):
        overrides[prefix + '.' + p] = hal.Type.BOOL
    if prefix != 'max-velocity':
        overrides[prefix + '.reset'] = hal.Type.BOOL

h = hal.component("python-ui")
for name in inputs:
    h.newpin(name, hal.Type.BOOL, hal.Dir.OUT)
for name, t in overrides.items():
    h.newpin(name, t, hal.Dir.OUT)
h.ready()
for name in list(inputs) + list(overrides):
    sig = 'sig-' + name.replace('.', '-')
    if os.system("halcmd net %s python-ui.%s task.%s" % (sig, name, name)):
        log("cannot connect task.%s" % name)
        sys.exit(1)

c = linuxcnc.command()
s = linuxcnc.stat()
e = linuxcnc.error_channel()


def pulse(name):
    h[name] = True
    time.sleep(0.05)
    h[name] = False
    time.sleep(0.05)


def wait_for(what, cond):
    start = time.time()
    while time.time() - start < 10.0:
        s.poll()
        if cond():
            log("%s: ok" % what)
            return
        time.sleep(0.05)
    log("timeout waiting for %s" % what)
    log("    task_state=%d task_mode=%d interp=%d state=%d" % (s.task_state, s.task_mode, s.interp_state, s.state))
    while True:
        err = e.poll()
        if not err:
            break
        log("    error channel: %s" % err[1])
    sys.exit(1)


def press(name, what, cond):
    pulse(name)
    wait_for("%s -> %s" % (name, what), cond)


press('estop.reset', 'estop reset', lambda: s.task_state == linuxcnc.STATE_ESTOP_RESET)
press('machine.on', 'machine on', lambda: s.task_state == linuxcnc.STATE_ON)

press('mode.mdi', 'mdi', lambda: s.task_mode == linuxcnc.MODE_MDI)
press('mode.manual', 'manual', lambda: s.task_mode == linuxcnc.MODE_MANUAL)

press('mist.on', 'mist on', lambda: s.mist)
press('mist.off', 'mist off', lambda: not s.mist)
press('flood.on', 'flood on', lambda: s.flood)
press('flood.off', 'flood off', lambda: not s.flood)

press('home-all', 'all homed', lambda: all(s.homed[:3]))
press('mode.joint', 'joint mode', lambda: s.motion_mode == linuxcnc.TRAJ_MODE_FREE)
# motion only unhomes in joint mode
press('joint.0.unhome', 'joint 0 unhomed', lambda: not s.homed[0])
press('joint.0.home', 'joint 0 homed', lambda: s.homed[0])
press('mode.teleop', 'teleop mode', lambda: s.motion_mode == linuxcnc.TRAJ_MODE_TELEOP)

press('program.optional-stop.on', 'optional stop on', lambda: s.optional_stop)
press('program.optional-stop.off', 'optional stop off', lambda: not s.optional_stop)
press('program.block-delete.on', 'block delete on', lambda: s.block_delete)
press('program.block-delete.off', 'block delete off', lambda: not s.block_delete)

press('spindle.0.forward', 'spindle forward', lambda: s.spindle[0]['direction'] == 1)
press('spindle.0.reverse', 'spindle reverse', lambda: s.spindle[0]['direction'] == -1)
press('spindle.0.stop', 'spindle stopped', lambda: s.spindle[0]['direction'] == 0)
press('spindle.0.start', 'spindle started', lambda: s.spindle[0]['direction'] == 1)
s.poll()
speed = abs(s.spindle[0]['speed'])
press('spindle.0.increase', 'spindle faster', lambda: abs(s.spindle[0]['speed']) > speed)
s.poll()
speed = abs(s.spindle[0]['speed'])
press('spindle.0.decrease', 'spindle slower', lambda: abs(s.spindle[0]['speed']) < speed)
press('spindle.0.stop', 'spindle stopped', lambda: s.spindle[0]['direction'] == 0)
press('spindle.0.brake-on', 'brake on', lambda: s.spindle[0]['brake'])
press('spindle.0.brake-off', 'brake off', lambda: not s.spindle[0]['brake'])

# a rejected internal command must not mark the UI's last command failed
c.mode(linuxcnc.MODE_MANUAL)
c.wait_complete()
pulse('program.step')       # can't step in manual mode
time.sleep(0.2)
s.poll()
if s.state != linuxcnc.RCS_DONE:
    log("rejected internal command changed the UI command status to %d" % s.state)
    sys.exit(1)
log("rejected internal command: UI status still done")

c.program_open(os.path.abspath("slow.ngc"))
c.wait_complete()
press('program.run', 'auto mode, program running',
      lambda: s.task_mode == linuxcnc.MODE_AUTO and
              s.interp_state in (linuxcnc.INTERP_READING, linuxcnc.INTERP_WAITING))
press('program.pause', 'paused', lambda: s.interp_state == linuxcnc.INTERP_PAUSED)
press('program.resume', 'running again',
      lambda: s.interp_state in (linuxcnc.INTERP_READING, linuxcnc.INTERP_WAITING) and s.current_vel > 0)
press('program.stop', 'stopped', lambda: s.interp_state == linuxcnc.INTERP_IDLE)
press('program.step', 'stepped (paused)', lambda: s.interp_state == linuxcnc.INTERP_PAUSED)
press('abort', 'aborted', lambda: s.interp_state == linuxcnc.INTERP_IDLE)

press('mode.auto', 'auto (no-op)', lambda: s.task_mode == linuxcnc.MODE_AUTO)
press('mode.manual', 'manual', lambda: s.task_mode == linuxcnc.MODE_MANUAL)


# overrides; the INI allows feed up to 1.5, spindle 0.5 .. 1.2 and a max
# velocity of 4
def value(what, get, expect):
    wait_for("%s -> %g" % (what, expect), lambda: abs(get() - expect) < 1e-9)


def override_value(prefix):
    return hal.get_value('task.' + prefix + '.value')


def check_override(prefix, steps):
    """steps: (action, arg, expected status value); actions set a pin or
    pulse one, then wait for both linuxcnc.stat and the .value pin."""
    get = {
        'feed-override': lambda: s.feedrate,
        'rapid-override': lambda: s.rapidrate,
        'spindle.0.override': lambda: s.spindle[0]['override'],
        'max-velocity': lambda: s.max_velocity,
    }[prefix]
    for action, arg, expect in steps:
        if action == 'pulse':
            pulse(prefix + '.' + arg)
            what = "%s.%s" % (prefix, arg)
        else:
            h[prefix + '.' + action] = arg
            time.sleep(0.05)
            what = "%s.%s = %r" % (prefix, action, arg)
        value(what, get, expect)
        value(what + " (value pin)", lambda: override_value(prefix), expect)


for prefix in ('feed-override', 'rapid-override', 'spindle.0.override'):
    h[prefix + '.count-enable'] = True
    h[prefix + '.scale'] = 0.1
h['max-velocity.count-enable'] = True
h['max-velocity.scale'] = 0.5

check_override('feed-override', [
    ('pulse', 'increase', 1.1),
    ('pulse', 'decrease', 1.0),
    ('counts', 3, 1.3),
    ('counts', 13, 1.5),            # clamped to MAX_FEED_OVERRIDE
    ('pulse', 'reset', 1.0),
    ('direct-value', True, 1.0),    # nothing happens until counts change
    ('counts', 7, 0.7),
    ('count-enable', False, 0.7),
    ('counts', 9, 0.7),             # ignored while disabled
    ('count-enable', True, 0.7),
    ('direct-value', False, 0.7),
    ('counts', 10, 0.8),
    ('counts', -3, 0.0),            # never below zero
    ('pulse', 'reset', 1.0),
])

# MPGs turned faster than status follows: every count must arrive. Feed
# and rapid change together once per Task cycle; Task runs one internal
# command per cycle, so the queue backs up and most changes are seen
# before the previous one has reached motion's status.
fo_counts = -3
ro_counts = 0
def turn(steps):
    global fo_counts, ro_counts
    for step in steps:
        fo_counts += step
        ro_counts += step
        h['feed-override.counts'] = fo_counts
        h['rapid-override.counts'] = ro_counts
        time.sleep(0.0011)

turn(([-1] * 5 + [1] * 3 + [-1] * 4 + [1] * 6) * 4)
value("fast feed-override counts", lambda: s.feedrate, 1.0)
value("fast rapid-override counts", lambda: s.rapidrate, 1.0)
turn([-1] * 4 + [1] * 2)
value("fast feed-override counts", lambda: s.feedrate, 0.8)
value("fast rapid-override counts", lambda: s.rapidrate, 0.8)
pulse('rapid-override.reset')
value("rapid-override.reset", lambda: s.rapidrate, 1.0)
pulse('feed-override.reset')
value("feed-override.reset", lambda: s.feedrate, 1.0)

check_override('rapid-override', [
    ('pulse', 'increase', 1.0),     # rapids never go above 100%
    ('pulse', 'decrease', 0.9),
    ('counts', ro_counts - 2, 0.7),
    ('pulse', 'reset', 1.0),
])

check_override('spindle.0.override', [
    ('pulse', 'decrease', 0.9),
    ('counts', -10, 0.5),           # MIN_SPINDLE_OVERRIDE
    ('pulse', 'increase', 0.6),
    ('direct-value', True, 0.6),
    ('counts', 20, 1.2),            # MAX_SPINDLE_OVERRIDE
    ('pulse', 'reset', 1.0),
])

check_override('max-velocity', [
    ('pulse', 'decrease', 3.5),
    ('direct-value', True, 3.5),
    ('counts', 4, 2.0),
    ('counts', 20, 4.0),            # [TRAJ]MAX_LINEAR_VELOCITY
    ('direct-value', False, 4.0),
    ('counts', 19, 3.5),
    ('pulse', 'increase', 4.0),
])

# Task reports a machine that is off but out of estop as ESTOP_RESET
press('machine.off', 'machine off', lambda: s.task_state == linuxcnc.STATE_ESTOP_RESET)

c.state(linuxcnc.STATE_ESTOP)
c.wait_complete()

log("all input pins work")
sys.exit(0)
