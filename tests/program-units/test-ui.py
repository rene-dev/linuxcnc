#!/usr/bin/env python3

# stat.program_units must follow the motion that is executing, like the
# G20/G21 entry in stat.gcodes, not the units the readahead has reached.

import linuxcnc
import linuxcnc_util
import os
import sys
import time

retval = 0

INCH = 1  # CANON_UNITS_INCHES
MM = 2    # CANON_UNITS_MM

c = linuxcnc.command()
s = linuxcnc.stat()
e = linuxcnc.error_channel()
l = linuxcnc_util.LinuxCNC(command=c, status=s, error=e)

l.wait_for_linuxcnc_startup()

def check(what, units, gcode):
    global retval
    s.poll()
    if s.program_units != units or gcode not in s.gcodes:
        print("%s: expected program_units %d and G%d, got %d and %s" %
              (what, units, gcode // 10, s.program_units, s.gcodes))
        retval = 1

c.state(linuxcnc.STATE_ESTOP_RESET)
c.state(linuxcnc.STATE_ON)

c.mode(linuxcnc.MODE_MDI)
c.wait_complete()
c.mdi("G20")
c.wait_complete()
check("MDI G20", INCH, 200)
c.mdi("G21")
c.wait_complete()
check("MDI G21", MM, 210)
c.mdi("G20")
c.wait_complete()
check("MDI G20 again", INCH, 200)

c.mode(linuxcnc.MODE_AUTO)
c.wait_complete()
c.program_open(os.path.join(os.getcwd(), "units.ngc"))
c.auto(linuxcnc.AUTO_RUN, 0)

# wait for the G21 move: by then the readahead has reached the closing G20
timeout = time.time() + 10
while True:
    s.poll()
    if s.motion_line == 2 and s.current_vel > 0:
        break
    if time.time() > timeout:
        print("timed out waiting for the move on line 2")
        sys.exit(1)
    time.sleep(0.01)
check("during the G21 move", MM, 210)

l.wait_for_interp_state(linuxcnc.INTERP_IDLE, timeout=10)
check("after the program", INCH, 200)

c.state(linuxcnc.STATE_OFF)
c.state(linuxcnc.STATE_ESTOP)

sys.exit(retval)
