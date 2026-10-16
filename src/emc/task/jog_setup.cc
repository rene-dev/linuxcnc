/********************************************************************
* Description: jog_setup.cc
*   The operator's jog setup held by task, see jog_setup.hh.
*
* License: GPL Version 2
********************************************************************/

#include "jog_setup.hh"
#include "nml_intf/emc_nml.hh"
#include <inifile.hh>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

using namespace linuxcnc;

namespace {

// Speed limits from the INI file, 0 = none.
double maxLinearSpeed = 0;
double maxAngularSpeed = 0;

// Axes named by [TRAJ]COORDINATES. Used until motion reports its axis mask.
int iniAxisMask = 0;

const char AXIS_LETTERS[] = "XYZABCUVW";

int axisMask(const EMC_STAT &stat)
{
    return stat.motion.traj.axis_mask ? stat.motion.traj.axis_mask : iniAxisMask;
}

double firstReal(IniFile &ini, const char *tag, const char *section1,
                 const char *section2, double fallback)
{
    if (auto v = ini.findReal(tag, section1)) return *v;
    if (auto v = ini.findReal(tag, section2)) return *v;
    return fallback;
}

double limit(double value, double max)
{
    return (max > 0 && value > max) ? max : value;
}

bool validSelection(const EMC_TASK_SET_JOG_SELECTION &msg, const EMC_STAT &stat)
{
    if (msg.index == EMC_JOG_SELECT_NONE) return true;
    if (msg.jjogmode) {
        return msg.index >= 0 && msg.index < stat.motion.traj.joints;
    }
    if (msg.index >= EMC_JOG_SELECT_AUX && msg.index < EMC_JOG_SELECT_AUX + EMC_JOG_SELECT_AUX_MAX) {
        return true;
    }
    return msg.index >= 0 && msg.index < EMCMOT_MAX_AXIS && (axisMask(stat) & (1 << msg.index));
}

} // namespace

int jogSetupInit(const char *inifile, EMC_STAT &stat)
{
    IniFile ini(inifile);
    if (!ini) {
        return -1;
    }

    std::string units = ini.findStringV("LINEAR_UNITS", "TRAJ", "mm");
    bool metric = units == "mm" || units == "metric" || units == "1.0";

    maxLinearSpeed = firstReal(ini, "MAX_LINEAR_VELOCITY", "DISPLAY", "TRAJ", 0);
    maxAngularSpeed = firstReal(ini, "MAX_ANGULAR_VELOCITY", "DISPLAY", "TRAJ", 0);

    // The fallbacks are the ones the GUIs used (common/iniinfo.py).
    EMC_TASK_STAT &task = stat.task;
    task.jogSpeed = limit(firstReal(ini, "DEFAULT_LINEAR_VELOCITY", "DISPLAY", "TRAJ",
                                    metric ? 25 : 1), maxLinearSpeed);
    task.jogAngularSpeed = limit(firstReal(ini, "DEFAULT_ANGULAR_VELOCITY", "DISPLAY", "TRAJ", 6),
                                 maxAngularSpeed);
    task.jogIncrement = 0;
    task.jogAngularIncrement = 0;

    iniAxisMask = 0;
    for (char c : ini.findStringV("COORDINATES", "TRAJ", "")) {
        const char *p = std::strchr(AXIS_LETTERS, std::toupper(static_cast<unsigned char>(c)));
        if (c && p) {
            iniAxisMask |= 1 << (p - AXIS_LETTERS);
        }
    }
    task.jogSelectedJoint = ini.findSIntV("JOINTS", "KINS", 0) > 0 ? 0 : EMC_JOG_SELECT_NONE;
    task.jogSelectedAxis = EMC_JOG_SELECT_NONE;
    for (int i = 0; i < EMCMOT_MAX_AXIS; i++) {
        if (iniAxisMask & (1 << i)) {
            task.jogSelectedAxis = i;
            break;
        }
    }
    return 0;
}

bool jogSetupIsCommand(NMLTYPE type)
{
    return type == EMC_TASK_SET_JOG_SPEED_TYPE
        || type == EMC_TASK_SET_JOG_INCREMENT_TYPE
        || type == EMC_TASK_SET_JOG_SELECTION_TYPE;
}

int jogSetupIssue(NMLmsg *cmd, EMC_STAT &stat)
{
    EMC_TASK_STAT &task = stat.task;

    switch (cmd->_type) {
    case EMC_TASK_SET_JOG_SPEED_TYPE: {
        auto msg = static_cast<EMC_TASK_SET_JOG_SPEED *>(cmd);
        if (!std::isfinite(msg->speed) || msg->speed < 0) {
            emcOperatorError("Invalid jog speed %g", msg->speed);
            return -1;
        }
        if (msg->angular) {
            task.jogAngularSpeed = limit(msg->speed, maxAngularSpeed);
        } else {
            task.jogSpeed = limit(msg->speed, maxLinearSpeed);
        }
        return 0;
    }
    case EMC_TASK_SET_JOG_INCREMENT_TYPE: {
        auto msg = static_cast<EMC_TASK_SET_JOG_INCREMENT *>(cmd);
        if (!std::isfinite(msg->increment) || msg->increment < 0) {
            emcOperatorError("Invalid jog increment %g", msg->increment);
            return -1;
        }
        if (msg->angular) {
            task.jogAngularIncrement = msg->increment;
        } else {
            task.jogIncrement = msg->increment;
        }
        return 0;
    }
    case EMC_TASK_SET_JOG_SELECTION_TYPE: {
        auto msg = static_cast<EMC_TASK_SET_JOG_SELECTION *>(cmd);
        if (!validSelection(*msg, stat)) {
            emcOperatorError("Can not select %s %d for jogging",
                             msg->jjogmode ? "joint" : "axis", msg->index);
            return -1;
        }
        if (msg->jjogmode) {
            task.jogSelectedJoint = msg->index;
        } else {
            task.jogSelectedAxis = msg->index;
        }
        return 0;
    }
    default:
        return -1;
    }
}
