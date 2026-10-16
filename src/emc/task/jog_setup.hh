/********************************************************************
* Description: jog_setup.hh
*   The operator's jog setup -- jog speeds, increments and the selected
*   joint and axis -- held by task so that every UI and halui share it.
*
*   The values live in EMC_TASK_STAT (jogSpeed et al.) and change only
*   through the EMC_TASK_SET_JOG_* commands. Jog commands still carry
*   their own speed and increment: a UI reads them from status.
*
* License: GPL Version 2
********************************************************************/
#ifndef TASK_JOG_SETUP_HH
#define TASK_JOG_SETUP_HH

#include "nml_intf/emc.hh"

class NMLmsg;
class EMC_STAT;

/* Set the startup values from the INI file: [DISPLAY]DEFAULT_LINEAR_VELOCITY
   and DEFAULT_ANGULAR_VELOCITY, continuous jogging, joint 0 and the first
   axis of [TRAJ]COORDINATES selected. Returns 0, or -1 if the INI file can
   not be read. */
int jogSetupInit(const char *inifile, EMC_STAT &stat);

/* True for the commands jogSetupIssue() handles. */
bool jogSetupIsCommand(NMLTYPE type);

/* Apply one EMC_TASK_SET_JOG_* command to the status. Speeds are limited to
   [DISPLAY]MAX_LINEAR_VELOCITY / MAX_ANGULAR_VELOCITY. Returns 0, or -1
   (with an operator error) for a value or selection that is refused. */
int jogSetupIssue(NMLmsg *cmd, EMC_STAT &stat);

#endif /* TASK_JOG_SETUP_HH */
