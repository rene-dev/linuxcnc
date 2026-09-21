/********************************************************************
* Description: nml_msg.hh
*   The base classes every EMC message derives from.
*
*   These come from libnml -- nml/nmlmsg.hh, nml/cmd_msg.hh and
*   nml/stat_msg.hh -- where they were the part of NML a message had to
*   inherit in order to be writable to a channel. There are no channels
*   any more: the transport is FlatBuffers over a websocket, generated
*   from emc/flatbuf/*.fbs. What is left is a plain message header --
*   a type tag, a size, and the few fields task and the UIs still read.
*
*   The names are kept as they were. Renaming them would touch some 200
*   call sites for no behavioural gain; see tasks.md.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* License: LGPL Version 2
* System: Linux
********************************************************************/

#ifndef LINUXCNC_NML_MSG_HH
#define LINUXCNC_NML_MSG_HH

#include <stddef.h>		// size_t
#include <stdint.h>

#include "rcs_status.hh"	// enum class RCS_STATUS

typedef int32_t NMLTYPE;

/* Base class for all EMC messages.
 *
 * The constructor is protected so that a message cannot be created
 * without deriving from it. A derived class passes its own type and its
 * own sizeof() up, and the constructor zeroes the whole derived object --
 * see clear(), which every EMC_* constructor relies on to start from a
 * known state before its own member initialisers run.
 */
class NMLmsg {
  protected:
    NMLmsg(NMLTYPE t, long s);
    NMLmsg(NMLTYPE t, size_t s);

  public:
    void clear();

    NMLTYPE _type;		/* each derived type has a unique id */
    long size;			/* so the whole buffer need not be copied */
};

class RCS_CMD_MSG:public NMLmsg {
  public:
    RCS_CMD_MSG(NMLTYPE t, long sz);

    /* Which command this is, within the connection that sent it. It used
       to be the shared command buffer's write id; task now hands it out
       itself, as a dispatch counter. */
    int serial_number;
};

class RCS_STAT_MSG:public NMLmsg {
  public:
    RCS_STAT_MSG(NMLTYPE t, size_t sz);

    NMLTYPE command_type;
    int echo_serial_number;
    RCS_STATUS status;
};

/* Operator message types. lib/python/linuxcnc.so exports the three
   numbers as module constants, and emcsched.hh uses the length as a
   buffer size. The NML_ERROR/NML_TEXT/NML_DISPLAY message classes that
   carried them over the error channel are gone with it. */
#define NML_ERROR_TYPE    ((NMLTYPE) 1)
#define NML_TEXT_TYPE     ((NMLTYPE) 2)
#define NML_DISPLAY_TYPE  ((NMLTYPE) 3)

#define NML_ERROR_LEN 256
#define NML_TEXT_LEN 256
#define NML_DISPLAY_LEN 256

#endif				/* LINUXCNC_NML_MSG_HH */
