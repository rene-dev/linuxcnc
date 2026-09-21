/********************************************************************
* Description: shcom.hh
*   Headers for the common functions that talk to task
*
*   These used to be NML calls. They now go over the websocket +
*   FlatBuffers transport (emc/ws_client), which replaces all three NML
*   channels -- command, status and error -- with one connection. The
*   function names and the EMC_STAT a caller reads are unchanged, so the
*   four programs built on this file did not have to be rewritten.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*   Further derived from work by jmkasunich, Alex Joni
*
* Author: Eric H. Johnson
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2007 All rights reserved.
*
* Last change:
********************************************************************/

#ifndef SHCOM_HH
#define SHCOM_HH

#include <cmath>
#include <string>

#include <linuxcnc.h>           // INCH_PER_MM
#include <inifile.hh>
#include "nml_intf/emc_nml.hh"
#include "ws_client/ws_client.hh"
#include "unitenum.hh"

static inline bool CLOSE(double a, double b, double eps)
{
    return std::fabs(a - b) < eps;
}
#define LINEAR_CLOSENESS 0.0001
#define ANGULAR_CLOSENESS 0.0001
#define CM_PER_MM 0.1
#define GRAD_PER_DEG (100.0/90.0)
#define RAD_PER_DEG TO_RAD	// from posemath.h
#define DEFAULT_PATH "../../nc_files/"

#define JOGTELEOP 0
#define JOGJOINT  1

extern LINEAR_UNIT_CONVERSION linearUnitConversion;
extern ANGULAR_UNIT_CONVERSION angularUnitConversion;

// serial number of the last command sent, to wait on. Unlike NML's, this
// one is handed out by our own connection, so it never collides with
// another program's commands.
extern int emcCommandSerialNumber;

// the connection to task. NULL until emcTaskConnect() succeeds.
extern linuxcnc::WsClient *emcClient;

// the status task last published, or NULL while disconnected. It points
// into emcClient and only changes when updateStatus() is called.
extern EMC_STAT *emcStatus;

extern std::string error_string;
extern std::string operator_text_string;
extern std::string operator_display_string;
extern std::string defaultPath;

// default value for timeout, 0 means wait forever
extern double emcTimeout;

enum EMC_UPDATE_TYPE {
    EMC_UPDATE_NONE = 1,
    EMC_UPDATE_AUTO
};
extern EMC_UPDATE_TYPE emcUpdateType;

enum EMC_WAIT_TYPE {
    EMC_WAIT_NEVER = 0,
    EMC_WAIT_RECEIVED = 2,
    EMC_WAIT_DONE
};
extern EMC_WAIT_TYPE emcWaitType;

// programStartLine is the saved valued of the line that
// sendProgramRun(int line) sent
extern int programStartLine;

// How far a command has got. Replaces comparing echo_serial_number and
// status out of the status buffer by hand -- there is no shared echo any
// more, and a result belongs to the connection that asked for it.
enum class EMC_CMD_STATE {
    pending,   // task has not taken it off its queue yet
    received,  // task has dispatched it and is working on it
    done,
    error      // it failed, or task refused to carry it
};

extern void strupr(char *s);
// Connect to task. Retries for retry_time seconds, in retry_interval
// steps, so that a UI started alongside linuxcnc does not have to race it.
extern int emcTaskConnect(double retry_time=10.0, double retry_interval=1.0);
extern void emcTaskDisconnect();
extern int updateStatus();
extern int updateError();
extern EMC_CMD_STATE emcCommandState(int serial);
extern int emcCommandWaitReceived();
extern int emcCommandWaitDone();
extern int emcCommandSend(RCS_CMD_MSG & cmd);
extern double convertLinearUnits(double u);
extern double convertAngularUnits(double u);
extern int sendDebug(int level);
extern int sendEstop();
extern int sendEstopReset();
extern int sendMachineOn();
extern int sendMachineOff();
extern int sendManual();
extern int sendAuto();
extern int sendMdi();
extern int sendOverrideLimits(int jnum);
extern int sendJogStop(int jnum, int jjogmode);
extern int sendJogCont(int jnum, int jjogmode, double speed);
extern int sendJogIncr(int jnum, int jjogmode, double speed, double incr);
extern int sendMistOn();
extern int sendMistOff();
extern int sendFloodOn();
extern int sendFloodOff();
extern int sendSpindleForward(int spindle);
extern int sendSpindleReverse(int spindle);
extern int sendSpindleOff(int spindle);
extern int sendSpindleIncrease(int spindle);
extern int sendSpindleDecrease(int spindle);
extern int sendSpindleConstant(int spindle);
extern int sendBrakeEngage(int spindle);
extern int sendBrakeRelease(int spindle);
extern int sendAbort();
extern int sendHome(int jnum);
extern int sendUnHome(int jnum);
extern int sendFeedOverride(double override);
extern int sendRapidOverride(double override);
extern int sendMaxVelocity(double velocity);
extern int sendSpindleOverride(int spindle, double override);
extern int sendTaskPlanInit();
extern int sendProgramOpen(const char *program);
extern int sendProgramRun(int line);
extern int sendProgramPause();
extern int sendProgramResume();
extern int sendSetOptionalStop(bool state);
extern int sendProgramStep();
extern int sendMdiCmd(const char *mdi);
extern int sendLoadToolTable(const char *file);
extern int sendToolSetOffset(int tool, double length, double diameter);
extern int sendJointSetBacklash(int jnum, double backlash);
extern int sendJointLoadComp(int joint, const char *file, int type);
extern int sendSetTeleopEnable(int enable);
extern int sendClearProbeTrippedFlag();
extern int sendProbe(double x, double y, double z);
extern int iniLoad(const char *filename);
extern int checkStatus();

#endif				/* ifndef SHCOM_HH */
// vim: ts=4 sw=4
