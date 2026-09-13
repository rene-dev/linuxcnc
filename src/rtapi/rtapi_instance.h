/********************************************************************
 * Description: rtapi_instance.h
 *
 *   Instance numbering, so that several independent LinuxCNC sessions
 *   can run on one machine at the same time.
 *
 *   Everything a session owns that lives in a system-wide namespace is
 *   shifted by the same amount: SysV shared memory keys, the SysV
 *   semaphores derived from them, and the NML TCP port numbers.  The
 *   shift is
 *
 *       LINUXCNC_INSTANCE * RTAPI_INSTANCE_STRIDE
 *
 *   With the variable unset (the normal case) the shift is zero and every
 *   key and port keeps the value it has always had, so an ordinary single
 *   session is unaffected.
 *
 *   RTAPI_INSTANCE_STRIDE has to be larger than the widest block of
 *   consecutive keys any one session allocates from a single base.  The
 *   widest today is sampler/streamer, which use base+channel for up to
 *   MAX_SAMPLERS (8) channels.
 *
 *   The instance number also selects the rtapi_app socket (see
 *   get_fifo_path() in uspace_rtapi_main.cc), so each instance gets its
 *   own realtime process as well as its own shared memory.
 *
 *   This covers the uspace builds.  The RTAI kernel modules in
 *   rtai_rtapi.c / rtai_ulapi.c are not shifted: there is one set of
 *   kernel modules on a machine, so there is nothing for a second
 *   instance to run in.
 *
 * Author: Rene Hopf
 * License: GPL Version 2
 ********************************************************************/

#ifndef RTAPI_INSTANCE_H
#define RTAPI_INSTANCE_H

#include <stdio.h>
#include <stdlib.h>

/* spacing between the key/port ranges of two adjacent instances */
#define RTAPI_INSTANCE_STRIDE 16

/* Keeps the shifted NML port numbers inside the 16 bit port range and the
   shifted keys well clear of the next base key. */
#define RTAPI_INSTANCE_MAX 1023

#define RTAPI_INSTANCE_ENV "LINUXCNC_INSTANCE"

#ifdef __cplusplus
extern "C" {
#endif

/* The instance number this process belongs to, 0 if LINUXCNC_INSTANCE is
   unset.  A value that is not a plain number in range is fatal: carrying on
   with instance 0 would silently share shared memory with whatever session
   is already running, which is exactly what the caller asked to avoid. */
static inline int rtapi_instance_number(void)
{
    static int instance = -1;
    const char *s;
    char *end;
    long v;

    if (instance >= 0) {
        return instance;
    }
    s = getenv(RTAPI_INSTANCE_ENV);
    if (s == NULL || *s == '\0') {
        instance = 0;
        return instance;
    }
    end = NULL;
    v = strtol(s, &end, 10);
    if (end == s || (end != NULL && *end != '\0') || v < 0 || v > RTAPI_INSTANCE_MAX) {
        fprintf(stderr,
                "%s=\"%s\" is not a whole number between 0 and %d\n",
                RTAPI_INSTANCE_ENV, s, RTAPI_INSTANCE_MAX);
        exit(1);
    }
    instance = (int)v;
    return instance;
}

/* Amount to add to a shared memory key or a TCP port number. */
static inline int rtapi_instance_offset(void)
{
    return rtapi_instance_number() * RTAPI_INSTANCE_STRIDE;
}

#ifdef __cplusplus
}
#endif

#endif /* RTAPI_INSTANCE_H */
