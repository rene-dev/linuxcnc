"""Instance numbering for parallel LinuxCNC sessions.

The Python side of src/rtapi/rtapi_instance.h.  Setting LINUXCNC_INSTANCE
to a whole number gives a session its own shared memory, sockets and
ports, so that several sessions can run on one machine without reaching
into each other.  Everything moves by the same stride, and an unset
variable means instance 0 and the port numbers LinuxCNC has always used.

Use port() for anything that binds or connects to a well known port:

    address = "tcp://127.0.0.1:%d" % linuxcnc_instance.port(5690)
"""

import os

# Keep in step with RTAPI_INSTANCE_STRIDE / RTAPI_INSTANCE_MAX in
# src/rtapi/rtapi_instance.h.
STRIDE = 16
MAX = 1023

ENV = 'LINUXCNC_INSTANCE'


def number():
    """This process's instance number, 0 if LINUXCNC_INSTANCE is unset.

    A value that is not a whole number in range raises ValueError rather
    than quietly falling back to 0: carrying on as instance 0 would share
    shared memory and ports with whatever session is already running,
    which is what the caller asked to avoid.
    """
    raw = os.environ.get(ENV)
    if raw is None or raw == '':
        return 0
    try:
        value = int(raw)
    except ValueError:
        value = -1
    if value < 0 or value > MAX:
        raise ValueError(
            '%s="%s" is not a whole number between 0 and %d' % (ENV, raw, MAX))
    return value


def offset():
    """Amount to add to a port number or shared memory key."""
    return number() * STRIDE


def port(base):
    """A well known port number, moved to this instance's range."""
    return base + offset()
