/********************************************************************
* Description: nml_msg.cc
*   Constructors for the EMC message base classes. See nml_msg.hh.
*
*   Carried over from libnml's nml/nmlmsg.cc, nml/cmd_msg.cc and
*   nml/stat_msg.cc. clear() is copied exactly: every EMC_* constructor
*   depends on it zeroing the whole derived object, not just the header.
*   The complaints went through libnml's rcs_print_error(); they go to
*   stderr now.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* License: LGPL Version 2
* System: Linux
********************************************************************/

#include "nml_msg.hh"

#include <stdio.h>		// fprintf()
#include <string.h>		// memset()

/* Both overloads exist because RCS_CMD_MSG is sized with a long and
   RCS_STAT_MSG with a size_t. They are kept separate rather than one
   delegating to the other: a negative long must fail the size check
   below, and casting it to size_t would make it pass. */
NMLmsg::NMLmsg(NMLTYPE t, long s)
{
    _type = t;
    size = s;
    clear();
    if (size < ((long) sizeof(NMLmsg))) {
	fprintf(stderr, "NMLmsg: size(=%ld) must be atleast %zu\n", size,
	    sizeof(NMLmsg));
	size = sizeof(NMLmsg);
    }
    if (_type <= 0) {
	fprintf(stderr, "NMLmsg: type(=%d) should be greater than zero.\n",
	    (int)_type);
    }
}

NMLmsg::NMLmsg(NMLTYPE t, size_t s)
{
    _type = t;
    size = s;
    clear();
    if (size < ((long) sizeof(NMLmsg))) {
	fprintf(stderr, "NMLmsg: size(=%ld) must be atleast %zu\n", size,
	    sizeof(NMLmsg));
	size = sizeof(NMLmsg);
    }
    if (_type <= 0) {
	fprintf(stderr, "NMLmsg: type(=%d) should be greater than zero.\n",
	    (int)_type);
    }
}

/* Zero the whole derived object, keeping the header that was just set.
   size is the derived class's sizeof(), so this reaches past NMLmsg --
   which is the point, and why it runs before the derived constructor's
   own member initialisers. */
void NMLmsg::clear()
{
    long temp_size;
    NMLTYPE temp_type;
    temp_size = size;
    temp_type = _type;
    memset((void *) this, 0, size);
    size = temp_size;
    _type = temp_type;
    if (size < ((long) sizeof(NMLmsg))) {
	fprintf(stderr, "NMLmsg: size(=%ld) must be atleast %zu\n", size,
	    sizeof(NMLmsg));
	size = sizeof(NMLmsg);
    }
}

RCS_CMD_MSG::RCS_CMD_MSG(NMLTYPE t, long sz):NMLmsg(t, sz)
{
    serial_number = 0;
}

RCS_STAT_MSG::RCS_STAT_MSG(NMLTYPE t, size_t sz):NMLmsg(t, sz)
{
    command_type = -1;
    echo_serial_number = -1;
    status = RCS_STATUS::UNINITIALIZED;
}
