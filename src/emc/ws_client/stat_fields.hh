/********************************************************************
* Description: stat_fields.hh
*   The flat attribute set of the Python `stat` object, as a table.
*
*   `stat` has presented EMC_STAT as about ninety flat attributes since
*   long before this transport existed, and that naming is the API: it is
*   `s.task_mode`, not `s.task.mode`. The mapping from one to the other is
*   the bulk of what the Python module does, so it lives here as data
*   rather than as ninety hand-written accessors, and the binding walks it.
*
*   Each list takes a macro of (python_name, path_inside_EMC_STAT). Adding
*   a field is one line here and nothing anywhere else.
*
*   Composite attributes -- poses, per-joint dicts, the code arrays -- are
*   not here: they are real methods on Status, because they build something
*   rather than read a field.
*
* License: GPL Version 2
********************************************************************/
#ifndef STAT_FIELDS_HH
#define STAT_FIELDS_HH

/* Read as Python ints. The enum-class ones are cast, deliberately: the
   module has always handed out plain ints for mode, state and friends, and
   the module constants (MODE_MDI, STATE_ON, ...) are what they compare
   against. */
#define STAT_INT_FIELDS(X) \
    X(state,                status)                        \
    X(task_mode,            task.mode)                     \
    X(task_state,           task.state)                    \
    X(exec_state,           task.execState)                \
    X(interp_state,         task.interpState)              \
    X(call_level,           task.callLevel)                \
    X(read_line,            task.readLine)                 \
    X(motion_line,          task.motionLine)               \
    X(current_line,         task.currentLine)              \
    X(program_units,        task.programUnits)             \
    X(interpreter_errcode,  task.interpreter_errcode)      \
    X(task_paused,          task.task_paused)              \
    X(queued_mdi_commands,  task.queuedMDIcommands)        \
    X(joints,               motion.traj.joints)            \
    X(spindles,             motion.traj.spindles)          \
    X(axis_mask,            motion.traj.axis_mask)         \
    X(motion_mode,          motion.traj.mode)              \
    X(queue,                motion.traj.queue)             \
    X(active_queue,         motion.traj.activeQueue)       \
    X(motion_id,            motion.traj.id)                \
    X(probe_val,            motion.traj.probeval)          \
    X(kinematics_type,      motion.traj.kinematics_type)   \
    X(motion_type,          motion.traj.motion_type)       \
    X(num_extrajoints,      motion.numExtraJoints)         \
    X(pocket_prepped,       io.tool.pocketPrepped)         \
    X(tool_in_spindle,      io.tool.toolInSpindle)         \
    X(tool_from_pocket,     io.tool.toolFromPocket)        \
    X(mist,                 io.coolant.mist)               \
    X(flood,                io.coolant.flood)              \
    X(estop,                io.aux.estop)                  \
    X(debug,                debug)

/* Read as Python bools. */
#define STAT_BOOL_FIELDS(X) \
    X(optional_stop,          task.optional_stop_state)          \
    X(block_delete,           task.block_delete_state)           \
    X(input_timeout,          task.input_timeout)                \
    X(enabled,                motion.traj.enabled)               \
    X(inpos,                  motion.traj.inpos)                 \
    X(queue_full,             motion.traj.queueFull)             \
    X(paused,                 motion.traj.paused)                \
    X(single_stepping,        motion.traj.single_stepping)       \
    X(probe_tripped,          motion.traj.probe_tripped)         \
    X(probing,                motion.traj.probing)               \
    X(feed_override_enabled,  motion.traj.feed_override_enabled) \
    X(adaptive_feed_enabled,  motion.traj.adaptive_feed_enabled) \
    X(feed_hold_enabled,      motion.traj.feed_hold_enabled)

#define STAT_DOUBLE_FIELDS(X) \
    X(rotation_xy,       task.rotation_xy)               \
    X(delay_left,        task.delayLeft)                 \
    X(linear_units,      motion.traj.linearUnits)        \
    X(angular_units,     motion.traj.angularUnits)       \
    X(cycle_time,        motion.traj.cycleTime)          \
    X(feedrate,          motion.traj.scale)              \
    X(rapidrate,         motion.traj.rapid_scale)        \
    X(velocity,          motion.traj.velocity)           \
    X(acceleration,      motion.traj.acceleration)       \
    X(max_velocity,      motion.traj.maxVelocity)        \
    X(max_acceleration,  motion.traj.maxAcceleration)    \
    X(distance_to_go,    motion.traj.distance_to_go)     \
    X(current_vel,       motion.traj.current_vel)

/* Fixed char[] in EMC_STAT; handed out as str. */
#define STAT_STRING_FIELDS(X) \
    X(file,          task.file)          \
    X(command,       task.command)       \
    X(ini_filename,  task.ini_filename)

/* Counters wide enough to need an unsigned 64-bit read. */
#define STAT_U64_FIELDS(X) \
    X(heartbeat,  motion.heartbeat)  \
    X(taskbeat,   task.taskbeat)

/* Nine-element poses, handed out as 9-tuples in x y z a b c u v w order. */
#define STAT_POSE_FIELDS(X) \
    X(g5x_offset,       task.g5x_offset)                 \
    X(g92_offset,       task.g92_offset)                 \
    X(tool_offset,      task.toolOffset)                 \
    X(position,         motion.traj.position)            \
    X(actual_position,  motion.traj.actualPosition)      \
    X(probed_position,  motion.traj.probedPosition)      \
    X(dtg,              motion.traj.dtg)

#endif /* STAT_FIELDS_HH */
