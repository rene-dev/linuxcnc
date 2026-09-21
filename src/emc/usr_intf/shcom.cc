/********************************************************************
* Description: shcom.cc
*   Common functions for talking to task
*
*   These used to be NML calls. The transport underneath is now the
*   websocket + FlatBuffers connection in emc/ws_client, which carries
*   commands, status and operator messages on one socket. Everything above
*   this file -- linuxcncrsh, linuxcnclcd, schedrmt and the Tcl binding --
*   kept its EMC_STAT field accesses and its send*() calls unchanged.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*   Further derived from work by jmkasunich, Alex Joni
*
* Author: Eric H. Johnson
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2006 All rights reserved.
*
* Last change:
********************************************************************/

#include "strutil.hh"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fmt/format.h>

#include <inifile.hh>
#include "rcs_status.hh"
#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"
#include "nml_intf/canon.hh"		// CANON_UNITS, CANON_UNITS_INCHES,MM,CM
#include "nml_intf/emcglb.h"		// emc_debug, TRAJ_MAX_VELOCITY, etc.
#include "nml_intf/emccfg.h"		// DEFAULT_TRAJ_MAX_VELOCITY
#include "timeutil.hh"             // esleep
#include "ws_defaults.hh"          // WS_DEFAULT_HOST, WS_DEFAULT_PORT
#include "mapini.hh"
#include "shcom.hh"             // Common communications functions

using namespace linuxcnc;
using namespace std::chrono_literals;

LINEAR_UNIT_CONVERSION linearUnitConversion = LINEAR_UNITS_AUTO;
ANGULAR_UNIT_CONVERSION angularUnitConversion = ANGULAR_UNITS_AUTO;

static int num_joints = EMCMOT_MAX_JOINTS;

int emcCommandSerialNumber;

// the connection to task, and the status snapshot it hands out
WsClient *emcClient;
EMC_STAT *emcStatus;

// where to look for task. The INI file's [TASK]WEBSOCKET_PORT has to agree
// with what task reads from the same key; the environment overrides both,
// which is how a UI reaches a task on another machine.
static std::string wsHost = WS_DEFAULT_HOST;
static int         wsPort = WS_DEFAULT_PORT;

std::string error_string;
std::string operator_text_string;
std::string operator_display_string;
std::string defaultPath = DEFAULT_PATH;
// default value for timeout, 0 means wait forever
double emcTimeout;
int programStartLine;

EMC_UPDATE_TYPE emcUpdateType;
EMC_WAIT_TYPE emcWaitType;

void strupr(char *s)
{
    while(*s) {
        *s = toupper((unsigned char)*s);
         s++;
    }
}

int emcTaskConnect(double retry_time, double retry_interval)
{
    if (emcClient != nullptr) {
        return 0;
    }

    if (const char *env = getenv("LINUXCNC_WS_HOST")) {
        if (*env != '\0') wsHost = env;
    }
    if (const char *env = getenv("LINUXCNC_WS_PORT")) {
        if (*env != '\0') wsPort = atoi(env);
    }

    if (wsPort <= 0) {
        fmt::print(stderr,
                   "shcom: the websocket transport is switched off "
                   "([TASK]WEBSOCKET_PORT = 0), so there is nothing to "
                   "connect to\n");
        return -1;
    }

    // One connection replaces all three NML channels, so unlike tryNml()
    // there is only one thing to wait for.
    auto *client = new WsClient();
    for (double end = retry_time; ; end -= retry_interval) {
        if (0 == client->connect(wsHost, wsPort, retry_interval > 0.0 ? retry_interval : 1.0) &&
            0 == client->waitStatus(retry_interval > 0.0 ? retry_interval : 1.0)) {
            // emcStatus points into the client for the rest of the run, so
            // there has to be a snapshot there before anyone reads it.
            emcClient = client;
            emcStatus = client->status();
            return 0;
        }
        if (end <= 0.0) {
            break;
        }
        esleep(retry_interval);
    }

    fmt::print(stderr, "shcom: {}\n", client->lastError());
    delete client;
    return -1;
}

void emcTaskDisconnect()
{
    if (emcClient == nullptr) {
        return;
    }
    WsClient *client = emcClient;
    emcClient = nullptr;
    emcStatus = nullptr;
    delete client;
}

int updateStatus()
{
    if (emcClient == nullptr) {
        return -1;
    }
    return emcClient->update();
}

/*
  updateError() updates "errors," which are true errors and also
  operator display and text messages.

  One message per call, as the NML error channel delivered them, so a
  caller that reads a string after each call still sees every message.
*/
int updateError()
{
    if (emcClient == nullptr) {
	return -1;
    }

    emcClient->update();

    OperatorMessage msg;
    if (!emcClient->nextMessage(msg)) {
	return 0;	// nothing new
    }

    switch (msg.kind) {
    case OperatorMessage::Kind::error:
	error_string = msg.text;
	break;
    case OperatorMessage::Kind::text:
	operator_text_string = msg.text;
	break;
    case OperatorMessage::Kind::display:
	operator_display_string = msg.text;
	break;
    }

    return 0;
}

EMC_CMD_STATE emcCommandState(int serial)
{
    if (emcClient == nullptr) {
	return EMC_CMD_STATE::error;
    }

    switch (emcClient->state(static_cast<unsigned long>(serial))) {
    case CmdState::pending:
	// With the connection gone the command will never be answered, so
	// report it failed rather than leave a caller polling forever.
	return emcClient->connected() ? EMC_CMD_STATE::pending
				      : EMC_CMD_STATE::error;
    case CmdState::received:
	return emcClient->connected() ? EMC_CMD_STATE::received
				      : EMC_CMD_STATE::error;
    case CmdState::error:
    case CmdState::refused:
	return EMC_CMD_STATE::error;
    case CmdState::done:
    case CmdState::unknown:
	// unknown means the acknowledgement has aged out, so the command
	// certainly finished. That is what an NML client concluded when the
	// echo had moved past its serial.
	return EMC_CMD_STATE::done;
    }
    return EMC_CMD_STATE::done;
}

int emcCommandWaitDone()
{
    if (emcClient == nullptr) {
	return -1;
    }
    return emcClient->waitDone(static_cast<unsigned long>(emcCommandSerialNumber),
			       emcTimeout);
}

int emcCommandWaitReceived()
{
    if (emcClient == nullptr) {
	return -1;
    }
    return emcClient->waitReceived(static_cast<unsigned long>(emcCommandSerialNumber),
				   emcTimeout);
}

int emcCommandSend(RCS_CMD_MSG & cmd)
{
    if (emcClient == nullptr) {
        return -1;
    }

    unsigned long serial = emcClient->send(cmd);
    if (serial == 0) {
        fmt::print(stderr, "shcom: {}\n", emcClient->lastError());
        return -1;
    }
    emcCommandSerialNumber = static_cast<int>(serial);
    return 0;
}

static inline int emcSendCommandAndWait(RCS_CMD_MSG& cmd)
{
    if (emcCommandSend(cmd))
        return -1;
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }
    return 0;
}


/*
  Unit conversion

  Length and angle units in the EMC status buffer are in user units, as
  defined in the INI file in [TRAJ] LINEAR,ANGULAR_UNITS. These may differ
  from the program units, and when they are the display is confusing.

  It may be desirable to synchronize the display units with the program
  units automatically, and also to break this sync and allow independent
  display of position values.

  The global variable "linearUnitConversion" is set by the Tcl commands
  emc_linear_unit_conversion to correspond to either "inch",
  "mm", "cm", "auto", or "custom". This forces numbers to be returned in the
  units specified, in program units when "auto" is set, or not converted
  at all if "custom" is specified.

  Ditto for "angularUnitConversion", set by emc_angular_unit_conversion
  to "deg", "rad", "grad", "auto", or "custom".

  With no args, emc_linear/angular_unit_conversion return the setting.

  The functions convertLinearUnits and convertAngularUnits take a length
  or angle value, typically from the emcStatus structure, and convert it
  as indicated by linearUnitConversion and angularUnitConversion, resp.
*/


/*
  to convert linear units, values are converted to mm, then to desired
  units
*/
double convertLinearUnits(double u)
{
    double in_mm;

    /* convert u to mm */
    in_mm = u / emcStatus->motion.traj.linearUnits;

    /* convert u to display units */
    switch (linearUnitConversion) {
    case LINEAR_UNITS_MM:
	return in_mm;
	break;
    case LINEAR_UNITS_INCH:
	return in_mm * INCH_PER_MM;
	break;
    case LINEAR_UNITS_CM:
	return in_mm * CM_PER_MM;
	break;
    case LINEAR_UNITS_AUTO:
	switch (emcStatus->task.programUnits) {
	case CANON_UNITS_MM:
	    return in_mm;
	    break;
	case CANON_UNITS_INCHES:
	    return in_mm * INCH_PER_MM;
	    break;
	case CANON_UNITS_CM:
	    return in_mm * CM_PER_MM;
	    break;
	}
	break;

    case LINEAR_UNITS_CUSTOM:
	return u;
	break;
    }

    // If it ever gets here we have an error.

    return u;
}

double convertAngularUnits(double u)
{
    // Angular units are always degrees
    return u;
}

int sendDebug(int level)
{
    EMC_SET_DEBUG debug_msg;

    debug_msg.debug = level;
    return emcSendCommandAndWait(debug_msg);
}

int sendEstop()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE::ESTOP;
    return emcSendCommandAndWait(state_msg);
}

int sendEstopReset()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE::ESTOP_RESET;
    return emcSendCommandAndWait(state_msg);
}

int sendMachineOn()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE::ON;
    return emcSendCommandAndWait(state_msg);
}

int sendMachineOff()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE::OFF;
    return emcSendCommandAndWait(state_msg);
}

int sendManual()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE::MANUAL;
    return emcSendCommandAndWait(mode_msg);
}

int sendAuto()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE::AUTO;
    return emcSendCommandAndWait(mode_msg);
}

int sendMdi()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE::MDI;
    return emcSendCommandAndWait(mode_msg);
}

int sendOverrideLimits(int joint)
{
    EMC_JOINT_OVERRIDE_LIMITS lim_msg;

    lim_msg.joint = joint;	// neg means off, else on for all
    return emcSendCommandAndWait(lim_msg);
}

int sendJogStop(int ja, int jjogmode)
{
    EMC_JOG_STOP emc_jog_stop_msg;

    if (   (   (jjogmode == JOGJOINT)
            && (emcStatus->motion.traj.mode == EMC_TRAJ_MODE::TELEOP) )
        || (   (jjogmode == JOGTELEOP )
            && (emcStatus->motion.traj.mode != EMC_TRAJ_MODE::TELEOP) )
       ) {
       return -1;
    }

    // FIXME: these checks should use the emcStatus values
    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
      fprintf(stderr,"shcom.cc: unexpected_1 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
      fprintf(stderr,"shcom.cc: unexpected_2 %d\n",ja); return -1;
    }

    emc_jog_stop_msg.jjogmode = jjogmode;
    emc_jog_stop_msg.joint_or_axis = ja;
    return emcSendCommandAndWait(emc_jog_stop_msg);
}

int sendJogCont(int ja, int jjogmode, double speed)
{
    EMC_JOG_CONT emc_jog_cont_msg;

    if (emcStatus->task.state != EMC_TASK_STATE::ON) { return -1; }
    if (   (  (jjogmode == JOGJOINT)
            && (emcStatus->motion.traj.mode == EMC_TRAJ_MODE::TELEOP) )
        || (   (jjogmode == JOGTELEOP )
            && (emcStatus->motion.traj.mode != EMC_TRAJ_MODE::TELEOP) )
       ) {
       return -1;
    }

    // FIXME: these checks should use the emcStatus values
    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
       fprintf(stderr,"shcom.cc: unexpected_3 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
       fprintf(stderr,"shcom.cc: unexpected_4 %d\n",ja); return -1;
    }

    emc_jog_cont_msg.jjogmode = jjogmode;
    emc_jog_cont_msg.joint_or_axis = ja;
    emc_jog_cont_msg.vel = speed / 60.0;

    return emcSendCommandAndWait(emc_jog_cont_msg);
}

int sendJogIncr(int ja, int jjogmode, double speed, double incr)
{
    EMC_JOG_INCR emc_jog_incr_msg;

    if (emcStatus->task.state != EMC_TASK_STATE::ON) { return -1; }
    if (   ( (jjogmode == JOGJOINT)
        && (  emcStatus->motion.traj.mode == EMC_TRAJ_MODE::TELEOP) )
        || ( (jjogmode == JOGTELEOP )
        && (  emcStatus->motion.traj.mode != EMC_TRAJ_MODE::TELEOP) )
       ) {
       return -1;
    }

    // FIXME: these checks should use the emcStatus values
    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
        fprintf(stderr,"shcom.cc: unexpected_5 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
        fprintf(stderr,"shcom.cc: unexpected_6 %d\n",ja); return -1;
    }

    emc_jog_incr_msg.jjogmode = jjogmode;
    emc_jog_incr_msg.joint_or_axis = ja;
    emc_jog_incr_msg.vel = speed / 60.0;
    emc_jog_incr_msg.incr = incr;

    return emcSendCommandAndWait(emc_jog_incr_msg);
}

int sendMistOn()
{
    EMC_COOLANT_MIST_ON emc_coolant_mist_on_msg;

    return emcSendCommandAndWait(emc_coolant_mist_on_msg);
}

int sendMistOff()
{
    EMC_COOLANT_MIST_OFF emc_coolant_mist_off_msg;

    return emcSendCommandAndWait(emc_coolant_mist_off_msg);
}

int sendFloodOn()
{
    EMC_COOLANT_FLOOD_ON emc_coolant_flood_on_msg;

    return emcSendCommandAndWait(emc_coolant_flood_on_msg);
}

int sendFloodOff()
{
    EMC_COOLANT_FLOOD_OFF emc_coolant_flood_off_msg;

    return emcSendCommandAndWait(emc_coolant_flood_off_msg);
}

int sendSpindleForward(int spindle)
{
    EMC_SPINDLE_ON emc_spindle_on_msg;
    emc_spindle_on_msg.spindle = spindle;
    if (emcStatus->task.activeSettings[2] != 0) {
	emc_spindle_on_msg.speed = fabs(emcStatus->task.activeSettings[2]);
    } else {
	emc_spindle_on_msg.speed = +500;
    }
    return emcSendCommandAndWait(emc_spindle_on_msg);
}

int sendSpindleReverse(int spindle)
{
    EMC_SPINDLE_ON emc_spindle_on_msg;
    emc_spindle_on_msg.spindle = spindle;
    if (emcStatus->task.activeSettings[2] != 0) {
	emc_spindle_on_msg.speed =
	    -1 * fabs(emcStatus->task.activeSettings[2]);
    } else {
	emc_spindle_on_msg.speed = -500;
    }
    return emcSendCommandAndWait(emc_spindle_on_msg);
}

int sendSpindleOff(int spindle)
{
    EMC_SPINDLE_OFF emc_spindle_off_msg;
    emc_spindle_off_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_off_msg);
}

int sendSpindleIncrease(int spindle)
{
    EMC_SPINDLE_INCREASE emc_spindle_increase_msg;
    emc_spindle_increase_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_increase_msg);
}

int sendSpindleDecrease(int spindle)
{
    EMC_SPINDLE_DECREASE emc_spindle_decrease_msg;
    emc_spindle_decrease_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_decrease_msg);
}

int sendSpindleConstant(int spindle)
{
    EMC_SPINDLE_CONSTANT emc_spindle_constant_msg;
    emc_spindle_constant_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_constant_msg);
}

int sendBrakeEngage(int spindle)
{
    EMC_SPINDLE_BRAKE_ENGAGE emc_spindle_brake_engage_msg;

    emc_spindle_brake_engage_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_brake_engage_msg);
}

int sendBrakeRelease(int spindle)
{
    EMC_SPINDLE_BRAKE_RELEASE emc_spindle_brake_release_msg;

    emc_spindle_brake_release_msg.spindle = spindle;
    return emcSendCommandAndWait(emc_spindle_brake_release_msg);
}

int sendAbort()
{
    EMC_TASK_ABORT task_abort_msg;

    return emcSendCommandAndWait(task_abort_msg);
}

int sendHome(int joint)
{
    EMC_JOINT_HOME emc_joint_home_msg;

    emc_joint_home_msg.joint = joint;
    return emcSendCommandAndWait(emc_joint_home_msg);
}

int sendUnHome(int joint)
{
    EMC_JOINT_UNHOME emc_joint_home_msg;

    emc_joint_home_msg.joint = joint;
    return emcSendCommandAndWait(emc_joint_home_msg);
}

int sendFeedOverride(double _override)
{
    EMC_TRAJ_SET_SCALE emc_traj_set_scale_msg;

    if (_override < 0.0) {
	_override = 0.0;
    }

    emc_traj_set_scale_msg.scale = _override;
    return emcSendCommandAndWait(emc_traj_set_scale_msg);
}

int sendRapidOverride(double _override)
{
    EMC_TRAJ_SET_RAPID_SCALE emc_traj_set_scale_msg;

    if (_override < 0.0) {
	_override = 0.0;
    }

    if (_override > 1.0) {
	_override = 1.0;
    }

    emc_traj_set_scale_msg.scale = _override;
    return emcSendCommandAndWait(emc_traj_set_scale_msg);
}


int sendSpindleOverride(int spindle, double _override)
{
    EMC_TRAJ_SET_SPINDLE_SCALE emc_traj_set_spindle_scale_msg;

    if (_override < 0.0) {
	_override = 0.0;
    }

    emc_traj_set_spindle_scale_msg.spindle = spindle;
    emc_traj_set_spindle_scale_msg.scale = _override;
    return emcSendCommandAndWait(emc_traj_set_spindle_scale_msg);
}

int sendTaskPlanInit()
{
    EMC_TASK_PLAN_INIT task_plan_init_msg;

    return emcSendCommandAndWait(task_plan_init_msg);
}

// saved value of last program opened
static std::string lastProgramFile;

int sendProgramOpen(const char *program)
{
    EMC_TASK_PLAN_OPEN msg;

    /* save this to run again */
    lastProgramFile = program;
    /* store filename in message */
    strxcpy(msg.file, program);

    /* NML could ship the file itself, in chunks, when the UI was on
       another machine than task (remote_buffer/remote_filesize). The
       websocket transport does not carry that yet, so the name is all task
       gets and the file has to be readable where task runs. See notes.md. */
    msg.remote_buffersize = 0;
    msg.remote_filesize = 0;

    return emcSendCommandAndWait(msg);
}

int sendProgramRun(int line)
{
    EMC_TASK_PLAN_RUN emc_task_plan_run_msg;

    if (emcUpdateType == EMC_UPDATE_AUTO) {
	updateStatus();
    }
    // first reopen program if it's not open
    if (0 == emcStatus->task.file[0]) {
	// send a request to open last one
	sendProgramOpen(lastProgramFile.c_str());
    }
    // save the start line, to compare against active line later
    programStartLine = line;

    emc_task_plan_run_msg.line = line;
    return emcSendCommandAndWait(emc_task_plan_run_msg);
}

int sendProgramPause()
{
    EMC_TASK_PLAN_PAUSE emc_task_plan_pause_msg;

    return emcSendCommandAndWait(emc_task_plan_pause_msg);
}

int sendProgramResume()
{
    EMC_TASK_PLAN_RESUME emc_task_plan_resume_msg;

    return emcSendCommandAndWait(emc_task_plan_resume_msg);
}

int sendSetOptionalStop(bool state)
{
    EMC_TASK_PLAN_SET_OPTIONAL_STOP emc_task_plan_set_optional_stop_msg;

    emc_task_plan_set_optional_stop_msg.state = state;
    return emcSendCommandAndWait(emc_task_plan_set_optional_stop_msg);
}


int sendProgramStep()
{
    EMC_TASK_PLAN_STEP emc_task_plan_step_msg;

    // clear out start line, if we had a verify before it would be -1
    programStartLine = 0;

    return emcSendCommandAndWait(emc_task_plan_step_msg);
}

int sendMdiCmd(const char *mdi)
{
    EMC_TASK_PLAN_EXECUTE emc_task_plan_execute_msg;

    strxcpy(emc_task_plan_execute_msg.command, mdi);
    return emcSendCommandAndWait(emc_task_plan_execute_msg);
}

int sendLoadToolTable(const char *file)
{
    EMC_TOOL_LOAD_TOOL_TABLE emc_tool_load_tool_table_msg;

    strxcpy(emc_tool_load_tool_table_msg.file, file);
    return emcSendCommandAndWait(emc_tool_load_tool_table_msg);
}

int sendToolSetOffset(int toolno, double zoffset, double diameter)
{
    EMC_TOOL_SET_OFFSET emc_tool_set_offset_msg;

    emc_tool_set_offset_msg.toolno = toolno;
    emc_tool_set_offset_msg.offset.tran.z = zoffset;
    emc_tool_set_offset_msg.diameter = diameter;
    emc_tool_set_offset_msg.orientation = 0; // mill style tool table

    return emcSendCommandAndWait(emc_tool_set_offset_msg);
}

int sendToolSetOffset(int toolno, double zoffset, double xoffset,
                      double diameter, double frontangle, double backangle,
                      int orientation)
{
    EMC_TOOL_SET_OFFSET emc_tool_set_offset_msg;

    emc_tool_set_offset_msg.toolno = toolno;
    emc_tool_set_offset_msg.offset.tran.z = zoffset;
    emc_tool_set_offset_msg.offset.tran.x = xoffset;
    emc_tool_set_offset_msg.diameter = diameter;
    emc_tool_set_offset_msg.frontangle = frontangle;
    emc_tool_set_offset_msg.backangle = backangle;
    emc_tool_set_offset_msg.orientation = orientation;

    return emcSendCommandAndWait(emc_tool_set_offset_msg);
}

int sendJointSetBacklash(int joint, double backlash)
{
    EMC_JOINT_SET_BACKLASH emc_joint_set_backlash_msg;

    emc_joint_set_backlash_msg.joint = joint;
    emc_joint_set_backlash_msg.backlash = backlash;
    return emcSendCommandAndWait(emc_joint_set_backlash_msg);
}

int sendJointLoadComp(int /*joint*/, const char *file, int type)
{
    EMC_JOINT_LOAD_COMP emc_joint_load_comp_msg;

    strxcpy(emc_joint_load_comp_msg.file, file);
    emc_joint_load_comp_msg.type = type;
    return emcSendCommandAndWait(emc_joint_load_comp_msg);
}

int sendSetTeleopEnable(int enable)
{
    EMC_TRAJ_SET_TELEOP_ENABLE emc_set_teleop_enable_msg;

    emc_set_teleop_enable_msg.enable = enable;
    return emcSendCommandAndWait(emc_set_teleop_enable_msg);
}

int sendClearProbeTrippedFlag()
{
    EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG emc_clear_probe_tripped_flag_msg;

    return emcSendCommandAndWait(emc_clear_probe_tripped_flag_msg);
}

int sendProbe(double x, double y, double z)
{
    EMC_TRAJ_PROBE emc_probe_msg;

    emc_probe_msg.pos.tran.x = x;
    emc_probe_msg.pos.tran.y = y;
    emc_probe_msg.pos.tran.z = z;

    return emcSendCommandAndWait(emc_probe_msg);
}

int iniLoad(const char *filename)
{
    IniFile inifile(filename);

    if (!inifile) {
	return -1;
    }

    // EMC debugging flags
    emc_debug = (unsigned)inifile.findUIntV("DEBUG", "EMC", 0);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        std::string version = inifile.findStringV("VERSION", "EMC", "<unknown>");
        std::string machine = inifile.findStringV("MACHINE", "EMC", "<unknown>");
        extern char *program_invocation_short_name;
        fmt::print(
            "{} ({}) shcom: machine '{}'  version '{}'\n",
            program_invocation_short_name, getpid(), machine, version
        );
    }

    // Where task listens. It has to match what task reads from the same
    // key; both default to WS_DEFAULT_PORT, so a config normally says
    // nothing at all.
    wsPort = inifile.findSIntV("WEBSOCKET_PORT", "TASK", WS_DEFAULT_PORT);

    if (auto inival = mapLinearUnits(inifile, "LINEAR_UNITS", "DISPLAY")) {
        linearUnitConversion = *inival;
    } else {
        linearUnitConversion = LINEAR_UNITS_AUTO;
    }

    if (auto inival = mapAngularUnits(inifile, "ANGULAR_UNITS", "DISPLAY")) {
        angularUnitConversion = *inival;
    } else {
        angularUnitConversion = ANGULAR_UNITS_AUTO;
    }

    return 0;
}

int checkStatus ()
{
    return (emcClient != nullptr && emcStatus != nullptr) ? 1 : 0;
}

