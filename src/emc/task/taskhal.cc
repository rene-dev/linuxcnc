/********************************************************************
* Description: taskhal.cc
*   HAL pins exported by Task ("task.*").
*
*   These are the halui pins that motion and iocontrol do not already
*   provide. Status pins are computed the same way halui computes them,
*   but directly from Task's own status once per Task cycle. Command pins
*   act on their rising edge, like halui's, and queue the same commands
*   halui would send, as Task-internal commands.
*
* License: GPL Version 2
* System: Linux
********************************************************************/

#include <math.h>
#include <string.h>
#include <hal.h>
#include <posemath.h>		// TO_RAD
#include <inifile.hh>
#include "taskhal.hh"
#include "task.hh"			// emcTaskQueueInternalCommand()
#include "nml_intf/emcglb.h"		// emc_inifile
#include "logutil.hh"
#include "timeutil.hh"			// linuxcnc::etime()

#define TASKHAL "task"

using namespace linuxcnc;

// the inputs of one override (feed, rapid, spindle or max-velocity)
struct override_pins {
    hal_sint_t counts;          // usually an MPG
    hal_bool_t count_enable;
    hal_bool_t direct_value;    // counts * scale instead of a relative change
    hal_real_t scale;
    hal_bool_t increase;        // rising edge: += scale
    hal_bool_t decrease;        // rising edge: -= scale
    hal_bool_t reset;           // rising edge: 1.0 (not for max-velocity)
};

// what an override's inputs were last cycle, and the value last queued
struct override_state {
    rtapi_s32 counts;
    bool increase, decrease, reset;
    NMLmsg *queued;             // our command, while still in the queue
    unsigned long ticket;
    bool pending;               // a queued value status doesn't show yet
    double pending_value;
    double pending_since;
};

static const char axis_letters[] = "xyzabcuvw";

struct taskhal_str {
    hal_real_t units_per_mm;

    hal_bool_t mode_is_manual;
    hal_bool_t mode_is_auto;
    hal_bool_t mode_is_mdi;
    hal_bool_t mode_is_joint;

    hal_bool_t program_is_idle;
    hal_bool_t program_is_running;
    hal_bool_t program_is_paused;
    hal_bool_t program_os_is_on;
    hal_bool_t program_bd_is_on;

    hal_real_t mv_value;
    hal_real_t fo_value;
    hal_real_t ro_value;
    hal_real_t so_value[EMCMOT_MAX_SPINDLES];

    hal_bool_t joint_on_soft_min_limit[EMCMOT_MAX_JOINTS];
    hal_bool_t joint_on_soft_max_limit[EMCMOT_MAX_JOINTS];
    hal_bool_t joint_override_limits[EMCMOT_MAX_JOINTS];

    hal_real_t axis_pos_feedback[EMCMOT_MAX_AXIS];
    hal_real_t axis_pos_relative[EMCMOT_MAX_AXIS];

    hal_real_t tool_diameter;

    // command inputs, acting on the rising edge
    hal_bool_t machine_on;
    hal_bool_t machine_off;
    hal_bool_t estop_reset;
    hal_bool_t mode_manual;
    hal_bool_t mode_auto;
    hal_bool_t mode_mdi;
    hal_bool_t mode_teleop;
    hal_bool_t mode_joint;
    hal_bool_t mist_on;
    hal_bool_t mist_off;
    hal_bool_t flood_on;
    hal_bool_t flood_off;
    hal_bool_t program_run;
    hal_bool_t program_pause;
    hal_bool_t program_resume;
    hal_bool_t program_step;
    hal_bool_t program_stop;
    hal_bool_t program_os_on;
    hal_bool_t program_os_off;
    hal_bool_t program_bd_on;
    hal_bool_t program_bd_off;
    hal_bool_t abort;
    hal_bool_t home_all;
    hal_bool_t joint_home[EMCMOT_MAX_JOINTS];
    hal_bool_t joint_unhome[EMCMOT_MAX_JOINTS];
    hal_bool_t spindle_start[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_stop[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_forward[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_reverse[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_increase[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_decrease[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_brake_on[EMCMOT_MAX_SPINDLES];
    hal_bool_t spindle_brake_off[EMCMOT_MAX_SPINDLES];

    override_pins mv;
    override_pins fo;
    override_pins ro;
    override_pins so[EMCMOT_MAX_SPINDLES];
};

// previous values of the command inputs, for edge detection
struct taskhal_inputs {
    bool machine_on, machine_off, estop_reset;
    bool mode_manual, mode_auto, mode_mdi, mode_teleop, mode_joint;
    bool mist_on, mist_off, flood_on, flood_off;
    bool program_run, program_pause, program_resume, program_step, program_stop;
    bool program_os_on, program_os_off, program_bd_on, program_bd_off;
    bool abort, home_all;
    bool joint_home[EMCMOT_MAX_JOINTS], joint_unhome[EMCMOT_MAX_JOINTS];
    bool spindle_start[EMCMOT_MAX_SPINDLES], spindle_stop[EMCMOT_MAX_SPINDLES];
    bool spindle_forward[EMCMOT_MAX_SPINDLES], spindle_reverse[EMCMOT_MAX_SPINDLES];
    bool spindle_increase[EMCMOT_MAX_SPINDLES], spindle_decrease[EMCMOT_MAX_SPINDLES];
    bool spindle_brake_on[EMCMOT_MAX_SPINDLES], spindle_brake_off[EMCMOT_MAX_SPINDLES];
    override_state mv, fo, ro, so[EMCMOT_MAX_SPINDLES];
};

static int comp_id = -1;
static taskhal_str *taskhal_data;
static int num_joints;
static int num_spindles;
static int axis_mask;
static bool have_home_all;
static taskhal_inputs old_inputs;

// override limits, from the same INI entries halui uses
static double max_feed_override;
static double min_spindle_override;
static double max_spindle_override;
static double max_max_velocity;

#define CHK(x) do { \
        int rv = (x); \
        if (rv < 0) { \
            log_error(TASKHAL ": cannot create HAL pin\n"); \
            hal_exit(comp_id); \
            comp_id = -1; \
            return rv; \
        } \
    } while (0)

// pins for one override, named prefix.counts etc.; max-velocity has no reset
static int override_pins_new(override_pins &p, bool with_reset, double scale,
                             const char *prefix)
{
    int rv;
    if ((rv = hal_pin_new_sint(comp_id, HAL_IN, &p.counts, 0, "%s.counts", prefix)) < 0 ||
        (rv = hal_pin_new_bool(comp_id, HAL_IN, &p.count_enable, 1, "%s.count-enable", prefix)) < 0 ||
        (rv = hal_pin_new_bool(comp_id, HAL_IN, &p.direct_value, 0, "%s.direct-value", prefix)) < 0 ||
        (rv = hal_pin_new_real(comp_id, HAL_IN, &p.scale, scale, "%s.scale", prefix)) < 0 ||
        (rv = hal_pin_new_bool(comp_id, HAL_IN, &p.increase, 0, "%s.increase", prefix)) < 0 ||
        (rv = hal_pin_new_bool(comp_id, HAL_IN, &p.decrease, 0, "%s.decrease", prefix)) < 0) {
        return rv;
    }
    if (with_reset) {
        return hal_pin_new_bool(comp_id, HAL_IN, &p.reset, 0, "%s.reset", prefix);
    }
    return 0;
}

int emcTaskHalInit(int joints, const EMC_STAT &stat)
{
    num_joints = joints;
    num_spindles = stat.motion.traj.spindles;
    axis_mask = stat.motion.traj.axis_mask;
    old_inputs = taskhal_inputs();

    // like halui, only offer home-all when there is a homing sequence
    {
        linuxcnc::IniFile inifile(emc_inifile);
        have_home_all = inifile && inifile.isSet("HOME_SEQUENCE", "JOINT_0");

        // only positive values count, like in halui
        auto positive = [&](const char *tag, const char *section, double def) {
            auto v = inifile ? inifile.findReal(tag, section) : std::nullopt;
            return v && *v > 0.0 ? *v : def;
        };
        max_feed_override = positive("MAX_FEED_OVERRIDE", "DISPLAY", 1.0);
        min_spindle_override = positive("MIN_SPINDLE_OVERRIDE", "DISPLAY", 0.0);
        max_spindle_override = positive("MAX_SPINDLE_OVERRIDE", "DISPLAY", 1.0);
        max_max_velocity = !inifile ? 1.0 :
            inifile.findRealV("MAX_LINEAR_VELOCITY", "TRAJ",
                              inifile.findRealV("MAX_VELOCITY", "AXIS_X", 1.0));
    }

    if (num_joints < 0 || num_joints > EMCMOT_MAX_JOINTS ||
        num_spindles < 0 || num_spindles > EMCMOT_MAX_SPINDLES) {
        log_error(TASKHAL ": bad joint ({}) or spindle ({}) count\n",
                  num_joints, num_spindles);
        return -1;
    }

    comp_id = hal_init(TASKHAL);
    if (comp_id < 0) {
        log_error(TASKHAL ": hal_init() failed\n");
        return -1;
    }

    taskhal_data = (taskhal_str *) hal_malloc(sizeof(taskhal_str));
    if (taskhal_data == NULL) {
        log_error(TASKHAL ": hal_malloc() failed\n");
        hal_exit(comp_id);
        comp_id = -1;
        return -1;
    }
    taskhal_str *d = taskhal_data;
    // pins that are not created stay null, e.g. max-velocity's reset
    memset(d, 0, sizeof(*d));

    CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->units_per_mm, 0.0, TASKHAL ".machine.units-per-mm"));

    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->mode_is_manual, 0, TASKHAL ".mode.is-manual"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->mode_is_auto, 0, TASKHAL ".mode.is-auto"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->mode_is_mdi, 0, TASKHAL ".mode.is-mdi"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->mode_is_joint, 0, TASKHAL ".mode.is-joint"));

    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->program_is_idle, 0, TASKHAL ".program.is-idle"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->program_is_running, 0, TASKHAL ".program.is-running"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->program_is_paused, 0, TASKHAL ".program.is-paused"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->program_os_is_on, 0, TASKHAL ".program.optional-stop.is-on"));
    CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->program_bd_is_on, 0, TASKHAL ".program.block-delete.is-on"));

    CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->mv_value, 0.0, TASKHAL ".max-velocity.value"));
    CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->fo_value, 0.0, TASKHAL ".feed-override.value"));
    CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->ro_value, 0.0, TASKHAL ".rapid-override.value"));
    for (int s = 0; s < num_spindles; s++) {
        CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->so_value[s], 0.0, TASKHAL ".spindle.%d.override.value", s));
    }

    for (int j = 0; j < num_joints; j++) {
        CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->joint_on_soft_min_limit[j], 0, TASKHAL ".joint.%d.on-soft-min-limit", j));
        CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->joint_on_soft_max_limit[j], 0, TASKHAL ".joint.%d.on-soft-max-limit", j));
        CHK(hal_pin_new_bool(comp_id, HAL_OUT, &d->joint_override_limits[j], 0, TASKHAL ".joint.%d.override-limits", j));
    }

    for (int a = 0; a < EMCMOT_MAX_AXIS; a++) {
        if (!(axis_mask & (1 << a))) { continue; }
        CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->axis_pos_feedback[a], 0.0, TASKHAL ".axis.%c.pos-feedback", axis_letters[a]));
        CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->axis_pos_relative[a], 0.0, TASKHAL ".axis.%c.pos-relative", axis_letters[a]));
    }

    CHK(hal_pin_new_real(comp_id, HAL_OUT, &d->tool_diameter, 0.0, TASKHAL ".tool.diameter"));

    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->machine_on, 0, TASKHAL ".machine.on"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->machine_off, 0, TASKHAL ".machine.off"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->estop_reset, 0, TASKHAL ".estop.reset"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mode_manual, 0, TASKHAL ".mode.manual"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mode_auto, 0, TASKHAL ".mode.auto"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mode_mdi, 0, TASKHAL ".mode.mdi"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mode_teleop, 0, TASKHAL ".mode.teleop"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mode_joint, 0, TASKHAL ".mode.joint"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mist_on, 0, TASKHAL ".mist.on"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->mist_off, 0, TASKHAL ".mist.off"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->flood_on, 0, TASKHAL ".flood.on"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->flood_off, 0, TASKHAL ".flood.off"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_run, 0, TASKHAL ".program.run"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_pause, 0, TASKHAL ".program.pause"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_resume, 0, TASKHAL ".program.resume"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_step, 0, TASKHAL ".program.step"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_stop, 0, TASKHAL ".program.stop"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_os_on, 0, TASKHAL ".program.optional-stop.on"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_os_off, 0, TASKHAL ".program.optional-stop.off"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_bd_on, 0, TASKHAL ".program.block-delete.on"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->program_bd_off, 0, TASKHAL ".program.block-delete.off"));
    CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->abort, 0, TASKHAL ".abort"));
    if (have_home_all) {
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->home_all, 0, TASKHAL ".home-all"));
    }
    for (int j = 0; j < num_joints; j++) {
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->joint_home[j], 0, TASKHAL ".joint.%d.home", j));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->joint_unhome[j], 0, TASKHAL ".joint.%d.unhome", j));
    }
    for (int s = 0; s < num_spindles; s++) {
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_start[s], 0, TASKHAL ".spindle.%d.start", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_stop[s], 0, TASKHAL ".spindle.%d.stop", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_forward[s], 0, TASKHAL ".spindle.%d.forward", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_reverse[s], 0, TASKHAL ".spindle.%d.reverse", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_increase[s], 0, TASKHAL ".spindle.%d.increase", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_decrease[s], 0, TASKHAL ".spindle.%d.decrease", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_brake_on[s], 0, TASKHAL ".spindle.%d.brake-on", s));
        CHK(hal_pin_new_bool(comp_id, HAL_IN, &d->spindle_brake_off[s], 0, TASKHAL ".spindle.%d.brake-off", s));
    }

    // halui's default scales: 0.1 for the overrides, 0 for max-velocity
    CHK(override_pins_new(d->mv, false, 0.0, TASKHAL ".max-velocity"));
    CHK(override_pins_new(d->fo, true, 0.1, TASKHAL ".feed-override"));
    CHK(override_pins_new(d->ro, true, 0.1, TASKHAL ".rapid-override"));
    for (int s = 0; s < num_spindles; s++) {
        char prefix[HAL_NAME_LEN + 1];
        snprintf(prefix, sizeof(prefix), TASKHAL ".spindle.%d.override", s);
        CHK(override_pins_new(d->so[s], true, 0.1, prefix));
    }

    hal_ready(comp_id);
    return 0;
}

void emcTaskHalExit()
{
    if (comp_id >= 0) {
        hal_exit(comp_id);
        comp_id = -1;
    }
    taskhal_data = NULL;
}

// the loaded tool's entry comes with the status; 0 for no tool, or a
// tool that is not in the tool table
static double tool_diameter(const EMC_TOOL_STAT &tool)
{
    const CANON_TOOL_TABLE &loaded = tool.toolTableCurrent;
    if (tool.toolInSpindle != 0 && loaded.toolno == tool.toolInSpindle) {
        return loaded.diameter;
    }
    return 0.0;
}

void emcTaskHalUpdate(const EMC_STAT &stat)
{
    taskhal_str *d = taskhal_data;
    if (!d) {
        return;
    }
    const EMC_TASK_STAT &task = stat.task;
    const EMC_TRAJ_STAT &traj = stat.motion.traj;

    hal_set_real(d->units_per_mm, traj.linearUnits);

    hal_set_bool(d->mode_is_manual, task.mode == EMC_TASK_MODE::MANUAL);
    hal_set_bool(d->mode_is_auto, task.mode == EMC_TASK_MODE::AUTO);
    hal_set_bool(d->mode_is_mdi, task.mode == EMC_TASK_MODE::MDI);
    hal_set_bool(d->mode_is_joint, traj.mode == EMC_TRAJ_MODE::FREE);

    hal_set_bool(d->program_is_idle, task.interpState == EMC_TASK_INTERP::IDLE);
    hal_set_bool(d->program_is_running, task.interpState == EMC_TASK_INTERP::READING ||
                                        task.interpState == EMC_TASK_INTERP::WAITING);
    hal_set_bool(d->program_is_paused, task.interpState == EMC_TASK_INTERP::PAUSED);
    hal_set_bool(d->program_os_is_on, task.optional_stop_state);
    hal_set_bool(d->program_bd_is_on, task.block_delete_state);

    hal_set_real(d->mv_value, traj.maxVelocity);
    hal_set_real(d->fo_value, traj.scale);
    hal_set_real(d->ro_value, traj.rapid_scale);
    for (int s = 0; s < num_spindles; s++) {
        hal_set_real(d->so_value[s], stat.motion.spindle[s].spindle_scale);
    }

    for (int j = 0; j < num_joints; j++) {
        const EMC_JOINT_STAT &joint = stat.motion.joint[j];
        hal_set_bool(d->joint_on_soft_min_limit[j], joint.minSoftLimit);
        hal_set_bool(d->joint_on_soft_max_limit[j], joint.maxSoftLimit);
        hal_set_bool(d->joint_override_limits[j], joint.overrideLimits);
    }

    // the relative position: the offset chain taken off in reverse, the
    // tool offset, G5x, the XY rotation, G92 and the tilted work plane
    const EmcPose &act = traj.actualPosition;
    double rel[EMCMOT_MAX_AXIS];
    double rx = act.tran.x - task.g5x_offset.tran.x - task.toolOffset.tran.x;
    double ry = act.tran.y - task.g5x_offset.tran.y - task.toolOffset.tran.y;
    double rz = act.tran.z - task.g5x_offset.tran.z - task.toolOffset.tran.z;
    {
        double t = -task.rotation_xy * TO_RAD;
        double x = rx * cos(t) - ry * sin(t);
        double y = ry * cos(t) + rx * sin(t);
        rx = x - task.g92_offset.tran.x;
        ry = y - task.g92_offset.tran.y;
        rz -= task.g92_offset.tran.z;
    }
    if (task.g68_active) {
        const double *r = task.g68_rotation;
        double x = rx - task.g68_offset.tran.x;
        double y = ry - task.g68_offset.tran.y;
        double z = rz - task.g68_offset.tran.z;
        rx = r[0]*x + r[3]*y + r[6]*z;
        ry = r[1]*x + r[4]*y + r[7]*z;
        rz = r[2]*x + r[5]*y + r[8]*z;
    }
    rel[0] = rx;
    rel[1] = ry;
    rel[2] = rz;
    rel[3] = act.a - task.g5x_offset.a - task.g92_offset.a - task.toolOffset.a;
    rel[4] = act.b - task.g5x_offset.b - task.g92_offset.b - task.toolOffset.b;
    rel[5] = act.c - task.g5x_offset.c - task.g92_offset.c - task.toolOffset.c;
    rel[6] = act.u - task.g5x_offset.u - task.g92_offset.u - task.toolOffset.u;
    rel[7] = act.v - task.g5x_offset.v - task.g92_offset.v - task.toolOffset.v;
    rel[8] = act.w - task.g5x_offset.w - task.g92_offset.w - task.toolOffset.w;

    const double fb[EMCMOT_MAX_AXIS] = {
        act.tran.x, act.tran.y, act.tran.z, act.a, act.b, act.c, act.u, act.v, act.w
    };
    for (int a = 0; a < EMCMOT_MAX_AXIS; a++) {
        if (!(axis_mask & (1 << a))) { continue; }
        hal_set_real(d->axis_pos_feedback[a], fb[a]);
        hal_set_real(d->axis_pos_relative[a], rel[a]);
    }

    hal_set_real(d->tool_diameter, tool_diameter(stat.io.tool));
}

// true on the rising edge of pin; old remembers the previous value
static bool rising(const hal_bool_t &pin, bool &old)
{
    bool v = hal_get_bool(pin);
    bool r = v && !old;
    old = v;
    return r;
}

// a change of pin in either direction; old remembers the previous value
static bool changed(const hal_bool_t &pin, bool &old)
{
    bool v = hal_get_bool(pin);
    bool r = v != old;
    old = v;
    return r;
}

template<class T>
static void queue(std::unique_ptr<T> msg)
{
    emcTaskQueueInternalCommand(std::move(msg));
}

template<class T>
static void queue()
{
    queue(std::make_unique<T>());
}

static void queue_state(EMC_TASK_STATE state)
{
    auto msg = std::make_unique<EMC_TASK_SET_STATE>();
    msg->state = state;
    queue(std::move(msg));
}

// like halui, a mode command for the current mode is not sent at all
static void queue_mode(const EMC_STAT &stat, EMC_TASK_MODE mode)
{
    if (stat.task.mode == mode) {
        return;
    }
    auto msg = std::make_unique<EMC_TASK_SET_MODE>();
    msg->mode = mode;
    queue(std::move(msg));
}

static void queue_teleop(int enable)
{
    auto msg = std::make_unique<EMC_TRAJ_SET_TELEOP_ENABLE>();
    msg->enable = enable;
    queue(std::move(msg));
}

static void queue_optional_stop(bool state)
{
    auto msg = std::make_unique<EMC_TASK_PLAN_SET_OPTIONAL_STOP>();
    msg->state = state;
    queue(std::move(msg));
}

static void queue_block_delete(bool state)
{
    auto msg = std::make_unique<EMC_TASK_PLAN_SET_BLOCK_DELETE>();
    msg->state = state;
    queue(std::move(msg));
}

static void queue_program_run(const EMC_STAT &stat)
{
    if (stat.task.file[0] == 0) {
        return; // no program open
    }
    queue_mode(stat, EMC_TASK_MODE::AUTO);
    auto msg = std::make_unique<EMC_TASK_PLAN_RUN>();
    msg->line = 0;
    queue(std::move(msg));
}

static void queue_home(int joint)
{
    auto msg = std::make_unique<EMC_JOINT_HOME>();
    msg->joint = joint;
    queue(std::move(msg));
}

static void queue_unhome(int joint)
{
    auto msg = std::make_unique<EMC_JOINT_UNHOME>();
    msg->joint = joint;
    queue(std::move(msg));
}

// dir is +1 or -1; the speed is the active S word, or 1 if there is none
static void queue_spindle_on(const EMC_STAT &stat, int spindle, int dir)
{
    auto msg = std::make_unique<EMC_SPINDLE_ON>();
    msg->spindle = spindle;
    double speed = fabs(stat.task.activeSettings[2]);
    msg->speed = dir * (speed != 0 ? speed : 1);
    queue(std::move(msg));
}

template<class T>
static void queue_spindle(int spindle)
{
    auto msg = std::make_unique<T>();
    msg->spindle = spindle;
    queue(std::move(msg));
}

static double clamp(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Queue the new value of an override. While our last command for it is
// still waiting in the queue, change that one instead, so a quickly turned
// MPG neither floods the queue nor lags behind.
template<class T>
static void queue_override(override_state &o, double T::*field, double v, int spindle)
{
    if (o.queued) {
        static_cast<T *>(o.queued)->*field = v;
        return;
    }
    auto msg = std::make_unique<T>();
    (*msg).*field = v;
    if constexpr (requires { msg->spindle; }) {
        msg->spindle = spindle;
    }
    T *raw = msg.get();
    o.ticket = emcTaskQueueInternalCommand(std::move(msg));
    o.queued = o.ticket ? raw : nullptr;
}

// Run one override's inputs, like halui: counts (relative, or counts *
// scale with direct-value), then increase, decrease and reset. Relative
// changes start from the value last queued until status shows it (or
// a second passed, e.g. when a GUI changed it meanwhile), so quick MPG
// counts don't get lost while a command is still on its way to motion.
template<class T>
static void check_override(override_pins &p, override_state &o, double current,
                           double lo, double hi, double T::*field, int spindle = 0)
{
    if (o.queued && emcTaskInternalCommandTaken(o.ticket)) {
        o.queued = nullptr;
    }
    // status is only meaningful once our command left the queue
    if (o.pending && !o.queued &&
        (current == o.pending_value || linuxcnc::etime() - o.pending_since > 1.0)) {
        o.pending = false;
    }
    double base = o.pending ? o.pending_value : current;
    double scale = hal_get_real(p.scale);
    bool have = false;
    double v = 0;

    rtapi_s32 counts = hal_get_si32(p.counts);
    if (counts != o.counts) {
        if (hal_get_bool(p.count_enable)) {
            if (hal_get_bool(p.direct_value)) {
                v = counts * scale;
            } else {
                // the difference survives the counter wrapping around
                rtapi_s32 delta = (rtapi_s32)((rtapi_u32)counts - (rtapi_u32)o.counts);
                v = base + delta * scale;
            }
            have = true;
        }
        o.counts = counts;
    }
    if (rising(p.increase, o.increase)) { v = (have ? v : base) + scale; have = true; }
    if (rising(p.decrease, o.decrease)) { v = (have ? v : base) - scale; have = true; }
    if (p.reset && rising(p.reset, o.reset)) { v = 1.0; have = true; }

    if (have) {
        v = clamp(v, lo, hi);
        queue_override(o, field, v, spindle);
        o.pending = true;
        o.pending_value = v;
        o.pending_since = linuxcnc::etime();
    }
}

void emcTaskHalCheckInputs(const EMC_STAT &stat)
{
    taskhal_str *d = taskhal_data;
    if (!d) {
        return;
    }
    taskhal_inputs &o = old_inputs;

    // same order as halui, so simultaneous edges queue in the same order
    if (rising(d->machine_on, o.machine_on)) queue_state(EMC_TASK_STATE::ON);
    if (rising(d->machine_off, o.machine_off)) queue_state(EMC_TASK_STATE::OFF);
    if (rising(d->estop_reset, o.estop_reset)) queue_state(EMC_TASK_STATE::ESTOP_RESET);

    if (rising(d->mode_manual, o.mode_manual)) queue_mode(stat, EMC_TASK_MODE::MANUAL);
    if (rising(d->mode_auto, o.mode_auto)) queue_mode(stat, EMC_TASK_MODE::AUTO);
    if (rising(d->mode_mdi, o.mode_mdi)) queue_mode(stat, EMC_TASK_MODE::MDI);
    if (rising(d->mode_teleop, o.mode_teleop)) queue_teleop(1);
    if (rising(d->mode_joint, o.mode_joint)) queue_teleop(0);

    if (rising(d->mist_on, o.mist_on)) queue<EMC_COOLANT_MIST_ON>();
    if (rising(d->mist_off, o.mist_off)) queue<EMC_COOLANT_MIST_OFF>();
    if (rising(d->flood_on, o.flood_on)) queue<EMC_COOLANT_FLOOD_ON>();
    if (rising(d->flood_off, o.flood_off)) queue<EMC_COOLANT_FLOOD_OFF>();

    if (rising(d->program_run, o.program_run)) queue_program_run(stat);
    if (rising(d->program_pause, o.program_pause)) queue<EMC_TASK_PLAN_PAUSE>();
    if (rising(d->program_os_on, o.program_os_on)) queue_optional_stop(true);
    if (rising(d->program_os_off, o.program_os_off)) queue_optional_stop(false);
    if (rising(d->program_bd_on, o.program_bd_on)) queue_block_delete(true);
    if (rising(d->program_bd_off, o.program_bd_off)) queue_block_delete(false);
    if (rising(d->program_resume, o.program_resume)) queue<EMC_TASK_PLAN_RESUME>();
    if (rising(d->program_step, o.program_step)) queue<EMC_TASK_PLAN_STEP>();
    if (rising(d->program_stop, o.program_stop)) queue<EMC_TASK_ABORT>();

    const EMC_TRAJ_STAT &traj = stat.motion.traj;
    check_override(d->mv, o.mv, traj.maxVelocity, 0.0, max_max_velocity,
                   &EMC_TRAJ_SET_MAX_VELOCITY::velocity);
    check_override(d->fo, o.fo, traj.scale, 0.0, max_feed_override,
                   &EMC_TRAJ_SET_SCALE::scale);
    check_override(d->ro, o.ro, traj.rapid_scale, 0.0, 1.0,
                   &EMC_TRAJ_SET_RAPID_SCALE::scale);

    for (int s = 0; s < num_spindles; s++) {
        check_override(d->so[s], o.so[s], stat.motion.spindle[s].spindle_scale,
                       min_spindle_override, max_spindle_override,
                       &EMC_TRAJ_SET_SPINDLE_SCALE::scale, s);
        if (rising(d->spindle_start[s], o.spindle_start[s])) queue_spindle_on(stat, s, +1);
        if (rising(d->spindle_stop[s], o.spindle_stop[s])) queue_spindle<EMC_SPINDLE_OFF>(s);
        if (rising(d->spindle_forward[s], o.spindle_forward[s])) queue_spindle_on(stat, s, +1);
        if (rising(d->spindle_reverse[s], o.spindle_reverse[s])) queue_spindle_on(stat, s, -1);
        // increase/decrease run while held, and fall back to constant on release
        if (changed(d->spindle_increase[s], o.spindle_increase[s])) {
            if (o.spindle_increase[s]) queue_spindle<EMC_SPINDLE_INCREASE>(s);
            else queue_spindle<EMC_SPINDLE_CONSTANT>(s);
        }
        if (changed(d->spindle_decrease[s], o.spindle_decrease[s])) {
            if (o.spindle_decrease[s]) queue_spindle<EMC_SPINDLE_DECREASE>(s);
            else queue_spindle<EMC_SPINDLE_CONSTANT>(s);
        }
        if (rising(d->spindle_brake_on[s], o.spindle_brake_on[s])) queue_spindle<EMC_SPINDLE_BRAKE_ENGAGE>(s);
        if (rising(d->spindle_brake_off[s], o.spindle_brake_off[s])) queue_spindle<EMC_SPINDLE_BRAKE_RELEASE>(s);
    }

    if (rising(d->abort, o.abort)) queue<EMC_TASK_ABORT>();
    if (have_home_all && rising(d->home_all, o.home_all)) queue_home(-1);

    for (int j = 0; j < num_joints; j++) {
        if (rising(d->joint_home[j], o.joint_home[j])) queue_home(j);
        if (rising(d->joint_unhome[j], o.joint_unhome[j])) queue_unhome(j);
    }
}
