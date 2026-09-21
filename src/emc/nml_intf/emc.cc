/********************************************************************
* Description: emc.cc
*   Maps an EMC message type to its name.
*
*   This file used to be the NML/CMS wire format for every EMC message:
*   emcFormat() plus one update() method per message class, all generated
*   by the NML CodeGen Java applet. Those encoded messages into a CMS
*   buffer, and nothing does that any more -- the transport is FlatBuffers
*   over a websocket, whose encoding is generated from emc/flatbuf/*.fbs.
*   The symbol lookup below is not NML machinery: it is used for debug
*   traces and for naming items on the interpreter list.
*
* License: GPL Version 2
* System: Linux
********************************************************************/

#include "emc.hh"
#include "emc_nml.hh"

const char *emc_symbol_lookup(uint32_t type)
{
    switch (type) {
    case EMC_AUX_STAT_TYPE:
	return "EMC_AUX_STAT";
    case EMC_JOINT_HALT_TYPE:
	return "EMC_JOINT_HALT";
    case EMC_JOINT_HOME_TYPE:
	return "EMC_JOINT_HOME";
    case EMC_JOINT_UNHOME_TYPE:
	return "EMC_JOINT_UNHOME";
    case EMC_JOG_CONT_TYPE:
	return "EMC_JOG_CONT";
    case EMC_JOG_INCR_TYPE:
	return "EMC_JOG_INCR";
    case EMC_JOG_ABS_TYPE:
	return "EMC_JOG_ABS";
    case EMC_JOG_STOP_TYPE:
	return "EMC_JOG_STOP";
    case EMC_JOINT_LOAD_COMP_TYPE:
	return "EMC_JOINT_LOAD_COMP";
    case EMC_JOINT_OVERRIDE_LIMITS_TYPE:
	return "EMC_JOINT_OVERRIDE_LIMITS";
    case EMC_JOINT_SET_FERROR_TYPE:
	return "EMC_JOINT_SET_FERROR";
    case EMC_JOINT_SET_BACKLASH_TYPE:
	return "EMC_JOINT_SET_BACKLASH";
    case EMC_JOINT_SET_HOMING_PARAMS_TYPE:
	return "EMC_JOINT_SET_HOMING_PARAMS";
    case EMC_JOINT_SET_MAX_POSITION_LIMIT_TYPE:
	return "EMC_JOINT_SET_MAX_POSITION_LIMIT";
    case EMC_JOINT_SET_MIN_FERROR_TYPE:
	return "EMC_JOINT_SET_MIN_FERROR";
    case EMC_JOINT_SET_MIN_POSITION_LIMIT_TYPE:
	return "EMC_JOINT_SET_MIN_POSITION_LIMIT";
    case EMC_JOINT_STAT_TYPE:
	return "EMC_JOINT_STAT";
    case EMC_COOLANT_FLOOD_OFF_TYPE:
	return "EMC_COOLANT_FLOOD_OFF";
    case EMC_COOLANT_FLOOD_ON_TYPE:
	return "EMC_COOLANT_FLOOD_ON";
    case EMC_COOLANT_MIST_OFF_TYPE:
	return "EMC_COOLANT_MIST_OFF";
    case EMC_COOLANT_MIST_ON_TYPE:
	return "EMC_COOLANT_MIST_ON";
    case EMC_COOLANT_STAT_TYPE:
	return "EMC_COOLANT_STAT";
    case EMC_IO_STAT_TYPE:
	return "EMC_IO_STAT";
    case EMC_MOTION_SET_AOUT_TYPE:
	return "EMC_MOTION_SET_AOUT";
    case EMC_MOTION_SET_DOUT_TYPE:
	return "EMC_MOTION_SET_DOUT";
    case EMC_MOTION_ADAPTIVE_TYPE:
	return "EMC_MOTION_ADAPTIVE";
    case EMC_MOTION_STAT_TYPE:
	return "EMC_MOTION_STAT";
    case EMC_NULL_TYPE:
	return "EMC_NULL";
    case EMC_OPERATOR_DISPLAY_TYPE:
	return "EMC_OPERATOR_DISPLAY";
    case EMC_OPERATOR_ERROR_TYPE:
	return "EMC_OPERATOR_ERROR";
    case EMC_OPERATOR_TEXT_TYPE:
	return "EMC_OPERATOR_TEXT";
    case EMC_SYSTEM_CMD_TYPE:
	return "EMC_SYSTEM_CMD";
    case EMC_SET_DEBUG_TYPE:
	return "EMC_SET_DEBUG";
    case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
	return "EMC_SPINDLE_BRAKE_ENGAGE";
    case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
	return "EMC_SPINDLE_BRAKE_RELEASE";
    case EMC_SPINDLE_CONSTANT_TYPE:
	return "EMC_SPINDLE_CONSTANT";
    case EMC_SPINDLE_DECREASE_TYPE:
	return "EMC_SPINDLE_DECREASE";
    case EMC_SPINDLE_INCREASE_TYPE:
	return "EMC_SPINDLE_INCREASE";
    case EMC_SPINDLE_OFF_TYPE:
	return "EMC_SPINDLE_OFF";
    case EMC_SPINDLE_ON_TYPE:
	return "EMC_SPINDLE_ON";
    case EMC_SPINDLE_SPEED_TYPE:
	return "EMC_SPINDLE_SPEED";
    case EMC_SPINDLE_ORIENT_TYPE:
	return "EMC_SPINDLE_ORIENT";
    case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
	return "EMC_SPINDLE_WAIT_ORIENT_COMPLETE";
    case EMC_SPINDLE_STAT_TYPE:
	return "EMC_SPINDLE_STAT";
    case EMC_STAT_TYPE:
	return "EMC_STAT";
    case EMC_TASK_ABORT_TYPE:
	return "EMC_TASK_ABORT";
    case EMC_TASK_PLAN_CLOSE_TYPE:
	return "EMC_TASK_PLAN_CLOSE";
    case EMC_TASK_PLAN_END_TYPE:
	return "EMC_TASK_PLAN_END";
    case EMC_TASK_PLAN_EXECUTE_TYPE:
	return "EMC_TASK_PLAN_EXECUTE";
    case EMC_TASK_PLAN_INIT_TYPE:
	return "EMC_TASK_PLAN_INIT";
    case EMC_TASK_PLAN_OPEN_TYPE:
	return "EMC_TASK_PLAN_OPEN";
    case EMC_TASK_PLAN_PAUSE_TYPE:
	return "EMC_TASK_PLAN_PAUSE";
    case EMC_TASK_PLAN_RESUME_TYPE:
	return "EMC_TASK_PLAN_RESUME";
    case EMC_TASK_PLAN_RUN_TYPE:
	return "EMC_TASK_PLAN_RUN";
    case EMC_TASK_PLAN_STEP_TYPE:
	return "EMC_TASK_PLAN_STEP";
    case EMC_TASK_PLAN_SYNCH_TYPE:
	return "EMC_TASK_PLAN_SYNCH";
    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
	return "EMC_TASK_PLAN_SET_OPTIONAL_STOP";
    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
	return "EMC_TASK_PLAN_SET_BLOCK_DELETE";
    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	return "EMC_TASK_PLAN_OPTIONAL_STOP";
    case EMC_TASK_SET_MODE_TYPE:
	return "EMC_TASK_SET_MODE";
    case EMC_TASK_SET_STATE_TYPE:
	return "EMC_TASK_SET_STATE";
    case EMC_TASK_STAT_TYPE:
	return "EMC_TASK_STAT";
    case EMC_TOOL_ABORT_TYPE:
	return "EMC_TOOL_ABORT";
    case EMC_TOOL_HALT_TYPE:
	return "EMC_TOOL_HALT";
    case EMC_TOOL_LOAD_TYPE:
	return "EMC_TOOL_LOAD";
    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
	return "EMC_TOOL_LOAD_TOOL_TABLE";
    case EMC_TOOL_PREPARE_TYPE:
	return "EMC_TOOL_PREPARE";
    case EMC_TOOL_SET_OFFSET_TYPE:
	return "EMC_TOOL_SET_OFFSET";
    case EMC_TOOL_SET_NUMBER_TYPE:
	return "EMC_TOOL_SET_NUMBER";
    case EMC_TOOL_STAT_TYPE:
	return "EMC_TOOL_STAT";
    case EMC_TOOL_UNLOAD_TYPE:
	return "EMC_TOOL_UNLOAD";
    case EMC_TRAJ_ABORT_TYPE:
	return "EMC_TRAJ_ABORT";
    case EMC_TRAJ_CIRCULAR_MOVE_TYPE:
	return "EMC_TRAJ_CIRCULAR_MOVE";
    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
	return "EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG";
    case EMC_TRAJ_DELAY_TYPE:
	return "EMC_TRAJ_DELAY";
    case EMC_TRAJ_LINEAR_MOVE_TYPE:
	return "EMC_TRAJ_LINEAR_MOVE";
    case EMC_TRAJ_PAUSE_TYPE:
	return "EMC_TRAJ_PAUSE";
    case EMC_TRAJ_PROBE_TYPE:
	return "EMC_TRAJ_PROBE";
    case EMC_AUX_INPUT_WAIT_TYPE:
	return "EMC_AUX_INPUT_WAIT";
    case EMC_TRAJ_RIGID_TAP_TYPE:
	return "EMC_TRAJ_RIGID_TAP";
    case EMC_TRAJ_RESUME_TYPE:
	return "EMC_TRAJ_RESUME";
    case EMC_TRAJ_SET_ACCELERATION_TYPE:
	return "EMC_TRAJ_SET_ACCELERATION";
    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
	return "EMC_TRAJ_SET_MAX_VELOCITY";
    case EMC_TRAJ_SET_MODE_TYPE:
	return "EMC_TRAJ_SET_MODE";
    case EMC_TRAJ_SET_OFFSET_TYPE:
	return "EMC_TRAJ_SET_OFFSET";
    case EMC_TRAJ_SELECT_KINS_TYPE:
	return "EMC_TRAJ_SELECT_KINS";
    case EMC_TRAJ_SET_G5X_TYPE:
	return "EMC_TRAJ_SET_G5X";
    case EMC_TRAJ_SET_G92_TYPE:
	return "EMC_TRAJ_SET_G92";
    case EMC_TRAJ_SET_ROTATION_TYPE:
	return "EMC_TRAJ_SET_ROTATION";
    case EMC_TRAJ_SET_SCALE_TYPE:
	return "EMC_TRAJ_SET_SCALE";
    case EMC_TRAJ_SET_RAPID_SCALE_TYPE:
	return "EMC_TRAJ_SET_RAPID_SCALE";
    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
	return "EMC_TRAJ_SET_SPINDLE_SCALE";
    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	return "EMC_TRAJ_SET_FO_ENABLE";
    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	return "EMC_TRAJ_SET_SO_ENABLE";
    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
	return "EMC_TRAJ_SET_FH_ENABLE";
    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE:
	return "EMC_TRAJ_SET_TELEOP_ENABLE";
    case EMC_TRAJ_SET_TERM_COND_TYPE:
	return "EMC_TRAJ_SET_TERM_COND";
    case EMC_TRAJ_SET_SPINDLESYNC_TYPE:
	return "EMC_TRAJ_SET_SPINDLESYNC";
    case EMC_TRAJ_SET_VELOCITY_TYPE:
	return "EMC_TRAJ_SET_VELOCITY";
    case EMC_TRAJ_STAT_TYPE:
	return "EMC_TRAJ_STAT";
    default:
	return "UNKNOWN";
	break;
    }
    return (NULL);
}
