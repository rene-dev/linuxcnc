#!/usr/bin/env python3
# The jog setup lives in task: every value set through linuxcnc.command
# shows up in linuxcnc.stat, in every task state and mode, and values that
# are out of range are refused or limited.

import os
import sys
import time
import hal
import linuxcnc
import linuxcnc_util

c = linuxcnc.command()
s = linuxcnc.stat()
e = linuxcnc.error_channel()

l = linuxcnc_util.LinuxCNC(command=c, status=s, error=e)
l.wait_for_linuxcnc_startup()

failures = 0


def check(name, ok, detail=''):
    global failures
    print('%s: %s %s' % ('ok' if ok else 'FAIL', name, detail))
    if not ok:
        failures += 1


def close(a, b):
    return abs(a - b) < 1e-9


def errors():
    out = []
    while True:
        m = e.poll()
        if not m:
            return out
        out.append(m[1])


def send(fn, *args):
    fn(*args)
    r = c.wait_complete()
    s.poll()
    return r


def startup():
    s.poll()
    check('linear speed from [DISPLAY]DEFAULT_LINEAR_VELOCITY', close(s.jog_speed, 0.5), s.jog_speed)
    check('angular speed from [DISPLAY]DEFAULT_ANGULAR_VELOCITY', close(s.jog_angular_speed, 10), s.jog_angular_speed)
    check('continuous jogging', s.jog_increment == 0 and s.jog_angular_increment == 0)
    check('joint 0 selected', s.jog_selected_joint == 0, s.jog_selected_joint)
    check('first configured axis (Y) selected', s.jog_selected_axis == 1, s.jog_selected_axis)


def settings(where):
    send(c.set_jog_speed, 1.5)
    check(where + ': linear speed', close(s.jog_speed, 1.5), s.jog_speed)
    send(c.set_jog_speed, 5.0)
    check(where + ': linear speed limited to [DISPLAY]MAX_LINEAR_VELOCITY', close(s.jog_speed, 2.0), s.jog_speed)
    send(c.set_jog_speed, 20, True)
    check(where + ': angular speed', close(s.jog_angular_speed, 20), s.jog_angular_speed)
    check(where + ': linear speed untouched by the angular one', close(s.jog_speed, 2.0))
    send(c.set_jog_speed, 100, True)
    check(where + ': angular speed limited', close(s.jog_angular_speed, 30), s.jog_angular_speed)

    send(c.set_jog_increment, 0.1)
    check(where + ': linear increment', close(s.jog_increment, 0.1), s.jog_increment)
    send(c.set_jog_increment, 5, True)
    check(where + ': angular increment', close(s.jog_angular_increment, 5), s.jog_angular_increment)
    send(c.set_jog_increment, 0)
    check(where + ': back to continuous', s.jog_increment == 0 and close(s.jog_angular_increment, 5))

    send(c.set_jog_selection, False, 3)
    check(where + ': select axis A', s.jog_selected_axis == 3, s.jog_selected_axis)
    send(c.set_jog_selection, False, linuxcnc.JOG_SELECT_AUX)
    check(where + ': select an aux target', s.jog_selected_axis == linuxcnc.JOG_SELECT_AUX)
    send(c.set_jog_selection, False, linuxcnc.JOG_SELECT_NONE)
    check(where + ': select no axis', s.jog_selected_axis == linuxcnc.JOG_SELECT_NONE)
    send(c.set_jog_selection, True, 2)
    check(where + ': select joint 2', s.jog_selected_joint == 2, s.jog_selected_joint)
    send(c.set_jog_selection, False, 1)
    check(where + ': axis selection leaves the joint alone', s.jog_selected_axis == 1 and s.jog_selected_joint == 2)
    errors()


def refused():
    for name, fn, args in (
            ('negative speed', c.set_jog_speed, (-1,)),
            ('NaN speed', c.set_jog_speed, (float('nan'),)),
            ('negative increment', c.set_jog_increment, (-0.1,)),
            ('axis X, not configured', c.set_jog_selection, (False, 0)),
            ('axis 9', c.set_jog_selection, (False, 9)),
            ('aux target past JOG_SELECT_AUX_MAX', c.set_jog_selection,
             (False, linuxcnc.JOG_SELECT_AUX + linuxcnc.JOG_SELECT_AUX_MAX)),
            ('joint 3, not configured', c.set_jog_selection, (True, 3))):
        s.poll()
        before = (s.jog_speed, s.jog_increment, s.jog_selected_axis, s.jog_selected_joint)
        r = send(fn, *args)
        after = (s.jog_speed, s.jog_increment, s.jog_selected_axis, s.jog_selected_joint)
        msgs = errors()
        check('refused: ' + name, r == linuxcnc.RCS_ERROR and before == after and msgs,
              'result=%s %r -> %r %r' % (r, before, after, msgs))


def wait(fn, timeout=2.0):
    end = time.time() + timeout
    while time.time() < end:
        s.poll()
        if fn():
            return True
        time.sleep(0.02)
    s.poll()
    return fn()


def pulse(pin):
    hal.set_p(pin, '1')
    time.sleep(0.1)
    hal.set_p(pin, '0')


def halui_pins():
    # halui pins set the values in task, halui status pins show them
    hal.set_p('halui.axis.jog-speed', '30')
    check('halui.axis.jog-speed sets the linear speed', wait(lambda: close(s.jog_speed, 0.5)), s.jog_speed)
    hal.set_p('halui.joint.jog-speed', '60')
    check('halui.joint.jog-speed sets the same speed', wait(lambda: close(s.jog_speed, 1.0)), s.jog_speed)
    hal.set_p('halui.axis.jog-speed-angular', '600')
    check('halui.axis.jog-speed-angular sets the angular speed',
          wait(lambda: close(s.jog_angular_speed, 10)), s.jog_angular_speed)
    hal.set_p('halui.axis.jog-increment', '0.01')
    check('halui.axis.jog-increment', wait(lambda: close(s.jog_increment, 0.01)), s.jog_increment)
    hal.set_p('halui.axis.jog-increment-angular', '0.5')
    check('halui.axis.jog-increment-angular', wait(lambda: close(s.jog_angular_increment, 0.5)))

    send(c.set_jog_speed, 1.5)
    check('halui.axis.jog-speed.value follows a GUI', wait(lambda: close(hal.get_value('halui.axis.jog-speed.value'), 90)),
          hal.get_value('halui.axis.jog-speed.value'))
    send(c.set_jog_speed, 15, True)
    check('halui.axis.jog-speed-angular.value', wait(lambda: close(hal.get_value('halui.axis.jog-speed-angular.value'), 900)))
    send(c.set_jog_increment, 0.2)
    check('halui.axis.jog-increment.value', wait(lambda: close(hal.get_value('halui.axis.jog-increment.value'), 0.2)))

    pulse('halui.joint.1.select')
    check('halui.joint.1.select', wait(lambda: s.jog_selected_joint == 1), s.jog_selected_joint)
    send(c.set_jog_selection, True, 0)
    check('halui joint pins follow a GUI', wait(lambda: hal.get_value('halui.joint.0.is-selected')
                                                    and not hal.get_value('halui.joint.1.is-selected')
                                                    and hal.get_value('halui.joint.selected') == 0))
    pulse('halui.axis.a.select')
    check('halui.axis.a.select', wait(lambda: s.jog_selected_axis == 3), s.jog_selected_axis)
    send(c.set_jog_selection, False, 2)
    check('halui axis pins follow a GUI', wait(lambda: hal.get_value('halui.axis.z.is-selected')
                                                   and not hal.get_value('halui.axis.a.is-selected')
                                                   and hal.get_value('halui.axis.selected') == 2))
    pulse('halui.jog-aux.1.select')
    check('halui.jog-aux.1.select', wait(lambda: s.jog_selected_axis == linuxcnc.JOG_SELECT_AUX + 1))
    check('aux selection: no axis is-selected, aux 1 is',
          wait(lambda: hal.get_value('halui.jog-aux.1.is-selected')
                       and not hal.get_value('halui.axis.z.is-selected')
                       and hal.get_value('halui.axis.selected') == 2))


def jv(j):
    return s.joint[j]['velocity']


def halui_jogs():
    # a jog through halui uses the speed in task and follows changes made
    # elsewhere while the button is held
    c.teleop_enable(0)
    c.wait_complete()
    send(c.set_jog_selection, True, 0)
    send(c.set_jog_speed, 1.5)
    wait(lambda: hal.get_value('halui.joint.selected') == 0)
    hal.set_p('halui.joint.selected.plus', '1')
    check('selected joint jogs at the speed in task', wait(lambda: close(jv(0), 1.5)), jv(0))
    send(c.set_jog_speed, 1.0)
    check('the jog follows a speed change from a GUI', wait(lambda: close(jv(0), 1.0)), jv(0))
    send(c.set_jog_selection, True, 1)
    check('a GUI selecting another joint stops the old one', wait(lambda: close(jv(0), 0)), jv(0))
    check('... and the held jog moves to the new one', wait(lambda: close(jv(1), 1.0)), jv(1))
    send(c.set_jog_selection, True, 2)
    check('an angular joint jogs at the angular speed', wait(lambda: close(jv(2), 15)), jv(2))
    send(c.set_jog_selection, True, linuxcnc.JOG_SELECT_NONE)
    check('selecting no joint stops the jog', wait(lambda: close(jv(2), 0) and close(jv(1), 0)))
    hal.set_p('halui.joint.selected.plus', '0')
    send(c.set_jog_selection, True, 0)
    time.sleep(0.2)
    check('nothing jogs after the release', close(jv(0), 0) and close(jv(1), 0) and close(jv(2), 0))


def gstat():
    # GStat (gmoccapy, qtvcp, GladeVCP) shows task's jog setup and sends
    # its changes there
    try:
        from common.hal_glib import GStat
    except ImportError as e:
        print('skip: GStat checks, %s' % e)
        return
    g = GStat()
    seen = []
    for sig in ('jograte-changed', 'jograte-angular-changed', 'jogincrement-changed',
                'joint-selection-changed', 'axis-selection-changed'):
        g.connect(sig, lambda w, *a, sig=sig: seen.append((sig,) + a))
    g.update()
    s.poll()
    check('GStat starts from task', close(g.get_jograte(), s.jog_speed * 60)
          and g.get_selected_axis() == 'XYZABCUVW'[s.jog_selected_axis], (g.get_jograte(), g.get_selected_axis()))

    del seen[:]
    send(c.set_jog_speed, 0.25)
    send(c.set_jog_increment, 0.05)
    send(c.set_jog_selection, False, 2)
    g.update()
    check('GStat emits a speed set elsewhere', ('jograte-changed', 15.0) in seen, seen)
    # GObject float signals are single precision
    check('GStat emits an increment set elsewhere',
          any(x[0] == 'jogincrement-changed' and abs(x[1] - 0.05) < 1e-6 and x[2] == '0.05' for x in seen), seen)
    check('GStat emits a selection made elsewhere', ('axis-selection-changed', 'Z') in seen, seen)
    del seen[:]
    g.update()
    check('... once', not seen, seen)

    g.set_jograte(90)
    s.poll()
    check('GStat.set_jograte sets task', close(s.jog_speed, 1.5) and close(g.get_jograte(), 90))
    g.set_jograte(600)
    s.poll()
    check('GStat takes the limited speed back', close(g.get_jograte(), 120) and close(s.jog_speed, 2.0), g.get_jograte())
    del seen[:]
    g.update()
    check('no second signal for its own change', not [x for x in seen if x[0] == 'jograte-changed'], seen)
    g.set_jograte_angular(300)
    s.poll()
    check('GStat.set_jograte_angular', close(s.jog_angular_speed, 5))
    g.set_jog_increments(0.1, '0.1 in')
    s.poll()
    check('GStat.set_jog_increments keeps the label', close(s.jog_increment, 0.1)
          and g.current_jog_distance_text == '0.1 in')
    g.set_selected_axis('A')
    s.poll()
    check('GStat.set_selected_axis', s.jog_selected_axis == 3 and g.get_selected_axis() == 'A')
    g.set_selected_axis('MPG0')
    s.poll()
    check('GStat aux target MPG0', s.jog_selected_axis == linuxcnc.JOG_SELECT_AUX and g.get_selected_axis() == 'MPG0')
    g.set_selected_axis('')
    s.poll()
    check('GStat selects no axis', s.jog_selected_axis == linuxcnc.JOG_SELECT_NONE and g.get_selected_axis() == '')
    g.set_selected_joint(1)
    s.poll()
    check('GStat.set_selected_joint', s.jog_selected_joint == 1 and g.get_selected_joint() == 1)
    g.set_selected_axis('X')
    s.poll()
    check('GStat keeps task\'s selection when task refuses one', s.jog_selected_axis == linuxcnc.JOG_SELECT_NONE
          and g.get_selected_axis() == '', g.get_selected_axis())
    errors()


startup()
gstat()
settings('ESTOP')
refused()

c.state(linuxcnc.STATE_ESTOP_RESET)
c.state(linuxcnc.STATE_ON)
c.wait_complete()
c.mode(linuxcnc.MODE_MDI)
c.wait_complete()
settings('ON, MDI')
c.mode(linuxcnc.MODE_AUTO)
c.wait_complete()
settings('ON, AUTO')

c.mode(linuxcnc.MODE_MANUAL)
c.wait_complete()
halui_pins()
halui_jogs()

c.state(linuxcnc.STATE_OFF)
c.wait_complete()
print('%d failures' % failures)
sys.stdout.flush()
# not sys.exit(): GStat's ZMQ sockets can keep the interpreter from exiting
os._exit(1 if failures else 0)
