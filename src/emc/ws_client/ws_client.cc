/********************************************************************
* Description: ws_client.cc
*   Client side of the websocket + FlatBuffers transport to task.
*   See ws_client.hh for what it is for.
*
*   Design notes:
*
*   - All socket work happens on one background thread running an
*     io_context. The calling thread never blocks on the socket: update()
*     takes what has already arrived, and send() posts a write. This is
*     what lets a UI keep a status snapshot that is always fresh without
*     having to poll a shared memory buffer the way NML did.
*
*   - Status is a full snapshot every task cycle, so a client that falls
*     behind simply loses intermediate frames; only the newest is kept.
*     Acknowledgements and operator messages are events and are queued, so
*     none is ever dropped in favour of a later one.
*
*   - encode_command() below is the mirror of decode_command() in
*     ws_server.cc. When one gains a command type, so must the other.
*
* License: GPL Version 2
********************************************************************/

#include "ws_client.hh"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>

#include "emc/flatbuf/emc_common_generated.h"
#include "emc/flatbuf/emc_stat_generated.h"
#include "emc/flatbuf/emc_cmd_generated.h"
#include "emc/flatbuf/emc_error_generated.h"
#include "emc/flatbuf/emc_ws_generated.h"

#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"

#include <atomic>
#include <chrono>
#include <future>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace asio      = boost::asio;
using tcp           = asio::ip::tcp;

namespace linuxcnc {

namespace {

/* asio reports a write cancelled by our own disconnect(); that is not
   something to complain about. */
inline beast::error_code net_error_operation_aborted()
{
    return beast::error_code(asio::error::operation_aborted);
}

/* Acks and operator messages are kept only so long as somebody might still
   ask about them. A serial that has fallen off the end reads back as
   unknown, which waitDone() treats as "finished long ago" -- the same
   conclusion an NML client drew when the echo had moved past its serial. */
const size_t MAX_ACKS     = 512;
const size_t MAX_MESSAGES = 256;

void copy_str(char *dst, size_t n, const flatbuffers::String *s)
{
    if (n == 0) return;
    if (s == nullptr) { dst[0] = '\0'; return; }
    size_t len = s->size();
    if (len > n - 1) len = n - 1;
    std::memcpy(dst, s->c_str(), len);
    dst[len] = '\0';
}

void to_emcpose(const EMC::Pose *p, EmcPose &out)
{
    if (p == nullptr) return;
    out.tran.x = p->x();
    out.tran.y = p->y();
    out.tran.z = p->z();
    out.a = p->a();
    out.b = p->b();
    out.c = p->c();
    out.u = p->u();
    out.v = p->v();
    out.w = p->w();
}

EMC::Pose from_emcpose(const EmcPose &p)
{
    return EMC::Pose(p.tran.x, p.tran.y, p.tran.z, p.a, p.b, p.c, p.u, p.v, p.w);
}

/* ------------------------------------------------------------------ */
/* FlatBuffer  ->  EMC_STAT                                           */
/* ------------------------------------------------------------------ */

void decode_task(const EMC::TaskStat *t, EMC_TASK_STAT &s)
{
    if (t == nullptr) return;

    s.mode        = static_cast<EMC_TASK_MODE>(t->mode());
    s.state       = static_cast<EMC_TASK_STATE>(t->state());
    s.execState   = static_cast<EMC_TASK_EXEC>(t->exec_state());
    s.interpState = static_cast<EMC_TASK_INTERP>(t->interp_state());
    s.callLevel   = t->call_level();
    s.motionLine  = t->motion_line();
    s.currentLine = t->current_line();
    s.readLine    = t->read_line();
    s.optional_stop_state = t->is_optional_stop_state();
    s.block_delete_state  = t->is_block_delete_state();
    s.input_timeout       = t->is_input_timeout();

    copy_str(s.file, sizeof(s.file), t->filename());
    copy_str(s.command, sizeof(s.command), t->command());
    copy_str(s.ini_filename, sizeof(s.ini_filename), t->ini_filename());

    to_emcpose(t->g5x_offset(), s.g5x_offset);
    s.g5x_index = t->g5x_index();
    to_emcpose(t->g92_offset(), s.g92_offset);
    s.rotation_xy = t->rotation_xy();
    to_emcpose(t->tool_offset(), s.toolOffset);

    if (auto v = t->active_g_codes()) {
        for (unsigned i = 0; i < v->size() && i < ACTIVE_G_CODES; i++)
            s.activeGCodes[i] = v->Get(i);
    }
    if (auto v = t->active_m_codes()) {
        for (unsigned i = 0; i < v->size() && i < ACTIVE_M_CODES; i++)
            s.activeMCodes[i] = v->Get(i);
    }
    if (auto v = t->active_settings()) {
        for (unsigned i = 0; i < v->size() && i < ACTIVE_SETTINGS; i++)
            s.activeSettings[i] = v->Get(i);
    }

    s.interpreter_errcode = t->interpreter_error_code();
    s.task_paused         = t->is_task_paused() ? 1 : 0;
    s.delayLeft           = t->delay_left();
    s.queuedMDIcommands   = t->queued_mdi_commands();
    s.programUnits        = static_cast<CANON_UNITS>(t->program_units());
    s.taskbeat            = t->taskbeat();
}

void decode_traj(const EMC::TrajStat *t, EMC_TRAJ_STAT &s)
{
    if (t == nullptr) return;

    s.linearUnits  = t->linear_units();
    s.angularUnits = t->angular_units();
    s.cycleTime    = t->cycle_time();
    s.joints       = t->joints();
    s.spindles     = t->spindles();
    s.axis_mask    = t->axis_mask();
    s.mode         = static_cast<EMC_TRAJ_MODE>(t->mode());
    s.enabled      = t->is_enabled();
    s.inpos        = t->is_in_position();
    s.queue        = t->queue();
    s.activeQueue  = t->active_queue();
    s.queueFull    = t->is_queue_full();
    s.id           = t->id();
    s.paused       = t->is_paused();
    s.scale        = t->scale();
    s.rapid_scale  = t->rapid_scale();
    to_emcpose(t->position(), s.position);
    to_emcpose(t->actual_position(), s.actualPosition);
    s.velocity        = t->velocity();
    s.acceleration    = t->acceleration();
    s.maxVelocity     = t->max_velocity();
    s.maxAcceleration = t->max_acceleration();
    to_emcpose(t->probed_position(), s.probedPosition);
    s.probe_tripped   = t->is_probe_tripped();
    s.probing         = t->is_probing();
    s.probeval        = t->probeval();
    s.kinematics_type = t->kinematics_type();
    s.motion_type     = t->motion_type();
    s.distance_to_go  = t->linear_distance_to_go();
    to_emcpose(t->distance_to_go(), s.dtg);
    s.current_vel           = t->current_vel();
    s.feed_override_enabled = t->is_feed_override_enabled();
    s.adaptive_feed_enabled = t->is_adaptive_feed_enabled();
    s.feed_hold_enabled     = t->is_feed_hold_enabled();
    s.single_stepping       = t->is_single_stepping();
}

void decode_motion(const EMC::MotionStat *m, EMC_MOTION_STAT &s)
{
    if (m == nullptr) return;

    decode_traj(m->traj(), s.traj);

    if (auto v = m->joint()) {
        for (unsigned i = 0; i < v->size(); i++) {
            const EMC::JointStat *j = v->Get(i);
            const int n = j->joint();
            if (n < 0 || n >= EMCMOT_MAX_JOINTS) continue;
            EMC_JOINT_STAT &d = s.joint[n];
            d.jointType = j->joint_type() == EMC::JointType::linear ? EMC_LINEAR
                                                                   : EMC_ANGULAR;
            d.units            = j->units();
            d.backlash         = j->backlash();
            d.minPositionLimit = j->min_position_limit();
            d.maxPositionLimit = j->max_position_limit();
            d.maxFerror        = j->max_ferror();
            d.minFerror        = j->min_ferror();
            d.ferrorCurrent    = j->ferror_current();
            d.ferrorHighMark   = j->ferror_high_mark();
            d.output           = j->output();
            d.input            = j->input();
            d.velocity         = j->velocity();
            d.inpos            = j->is_in_position();
            d.homing           = j->is_homing();
            d.homed            = j->is_homed();
            d.fault            = j->is_fault();
            d.enabled          = j->is_enabled();
            d.minSoftLimit     = j->is_min_soft_limit_exceeded();
            d.maxSoftLimit     = j->is_max_soft_limit_exceeded();
            d.minHardLimit     = j->is_min_hard_limit_exceeded();
            d.maxHardLimit     = j->is_max_hard_limit_exceeded();
            d.overrideLimits   = j->is_override_limits();
        }
    }

    if (auto v = m->axis()) {
        for (unsigned i = 0; i < v->size(); i++) {
            const EMC::AxisStat *a = v->Get(i);
            const int n = a->axis();
            if (n < 0 || n >= EMCMOT_MAX_AXIS) continue;
            s.axis[n].minPositionLimit = a->min_position_limit();
            s.axis[n].maxPositionLimit = a->max_position_limit();
            s.axis[n].velocity         = a->velocity();
        }
    }

    if (auto v = m->spindle()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_SPINDLES; i++) {
            const EMC::SpindleStat *p = v->Get(i);
            EMC_SPINDLE_STAT &d = s.spindle[i];
            d.speed         = p->speed();
            d.spindle_scale = p->spindle_scale();
            d.css_maximum   = p->css_maximum();
            d.css_factor    = p->css_factor();
            d.state         = p->state();
            d.direction     = p->direction();
            d.brake         = p->is_brake_engaged();
            /* One tri-state on the wire as two flags, because "increasing"
               reads better than a signed integer nobody remembers. */
            d.increasing    = p->is_increasing() ? 1 : (p->is_decreasing() ? -1 : 0);
            d.enabled       = p->is_enabled();
            d.orient_state  = p->orient_state();
            d.orient_fault  = p->orient_fault();
            d.spindle_override_enabled = p->is_spindle_override_enabled();
            d.homed         = p->is_homed();
        }
    }

    if (auto v = m->synch_di()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_DIO; i++)
            s.synch_di[i] = v->Get(i);
    }
    if (auto v = m->synch_do()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_DIO; i++)
            s.synch_do[i] = v->Get(i);
    }
    if (auto v = m->analog_input()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_AIO; i++)
            s.analog_input[i] = v->Get(i);
    }
    if (auto v = m->analog_output()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_AIO; i++)
            s.analog_output[i] = v->Get(i);
    }
    if (auto v = m->misc_error()) {
        for (unsigned i = 0; i < v->size() && i < EMCMOT_MAX_MISC_ERROR; i++)
            s.misc_error[i] = v->Get(i);
    }

    s.debug          = m->debug();
    s.on_soft_limit  = m->is_on_soft_limit();
    s.external_offsets_applied = m->is_external_offsets_applied();
    to_emcpose(m->eoffset_pose(), s.eoffset_pose);
    s.numExtraJoints = m->num_extra_joints();
    s.jogging_active = m->is_jogging_active();
    s.heartbeat      = m->heartbeat();

    s.echo_serial_number = m->echo_serial_number();
    s.command_type       = static_cast<NMLTYPE>(m->command_type());
    s.status             = static_cast<RCS_STATUS>(m->status());
}

void decode_io(const EMC::IOStat *io, EMC_IO_STAT &s)
{
    if (io == nullptr) return;

    s.debug  = io->debug();
    s.reason = io->reason();
    s.fault  = io->fault();

    if (auto t = io->tool()) {
        s.tool.pocketPrepped  = t->pocket_prepped();
        s.tool.toolInSpindle  = t->tool_in_spindle();
        s.tool.toolFromPocket = t->tool_from_pocket();
    }
    if (auto c = io->coolant()) {
        s.coolant.mist  = c->is_mist();
        s.coolant.flood = c->is_flood();
    }
    if (auto a = io->aux()) {
        s.aux.estop = a->is_estopped();
    }

    s.echo_serial_number = io->echo_serial_number();
    s.command_type       = static_cast<NMLTYPE>(io->command_type());
    s.status             = static_cast<RCS_STATUS>(io->status());
}

void decode_status(const EMC::EmcStat *m, EMC_STAT &s)
{
    if (m == nullptr) return;

    decode_task(m->task(), s.task);
    decode_motion(m->motion(), s.motion);
    decode_io(m->io(), s.io);

    s.echo_serial_number = m->echo_serial_number();
    s.command_type       = static_cast<NMLTYPE>(m->command_type());
    s.status             = static_cast<RCS_STATUS>(m->status());
    s.debug              = m->debug();

    /* Task sets these three from the top-level ones in the same breath, so
       deriving them here is what the status buffer would have said rather
       than a guess. The Tcl binding reads them (emc_task_command, etc.). */
    s.task.echo_serial_number = s.echo_serial_number;
    s.task.command_type       = s.command_type;
    s.task.status             = s.status;
}

/* ------------------------------------------------------------------ */
/* NML command  ->  FlatBuffer                                        */
/* ------------------------------------------------------------------ */

/* The mirror of decode_command() in ws_server.cc. Returns false for a
   command type the transport does not carry yet, which the caller reports
   rather than letting it vanish. */
bool encode_command(flatbuffers::FlatBufferBuilder &fbb,
                    const RCS_CMD_MSG &cmd, uint64_t serial)
{
    EMC::Command              utype = EMC::Command::NONE;
    flatbuffers::Offset<void> uval;

#define CMD(T) const T &c = static_cast<const T &>(cmd); (void)c

    switch (cmd._type) {
    case EMC_SET_DEBUG_TYPE: {
        CMD(EMC_SET_DEBUG);
        EMC::SetDebug s(static_cast<int>(c.debug));
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::set_debug;
        break;
    }
    case EMC_TASK_SET_STATE_TYPE: {
        CMD(EMC_TASK_SET_STATE);
        EMC::TaskSetState s(static_cast<int>(c.state));
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_set_state;
        break;
    }
    case EMC_TASK_SET_MODE_TYPE: {
        CMD(EMC_TASK_SET_MODE);
        EMC::TaskSetMode s(static_cast<int>(c.mode));
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_set_mode;
        break;
    }
    case EMC_TASK_ABORT_TYPE: {
        EMC::TaskAbort s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_abort;
        break;
    }
    case EMC_TASK_PLAN_EXECUTE_TYPE: {
        CMD(EMC_TASK_PLAN_EXECUTE);
        uval = EMC::CreateTaskPlanExecuteDirect(fbb, c.command).Union();
        utype = EMC::Command::task_plan_execute;
        break;
    }
    case EMC_TASK_PLAN_OPEN_TYPE: {
        CMD(EMC_TASK_PLAN_OPEN);
        uval = EMC::CreateTaskPlanOpenDirect(fbb, c.file).Union();
        utype = EMC::Command::task_plan_open;
        break;
    }
    case EMC_TASK_PLAN_RUN_TYPE: {
        CMD(EMC_TASK_PLAN_RUN);
        EMC::TaskPlanRun s(c.line);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_run;
        break;
    }
    case EMC_TASK_PLAN_PAUSE_TYPE: {
        EMC::TaskPlanPause s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_pause;
        break;
    }
    case EMC_TASK_PLAN_RESUME_TYPE: {
        EMC::TaskPlanResume s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_resume;
        break;
    }
    case EMC_TASK_PLAN_STEP_TYPE: {
        EMC::TaskPlanStep s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_step;
        break;
    }
    case EMC_TASK_PLAN_INIT_TYPE: {
        EMC::TaskPlanInit s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_init;
        break;
    }
    case EMC_TASK_PLAN_SYNCH_TYPE: {
        EMC::TaskPlanSynch s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_synch;
        break;
    }
    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE: {
        CMD(EMC_TASK_PLAN_SET_OPTIONAL_STOP);
        EMC::TaskPlanSetOptionalStop s(c.state);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_set_optional_stop;
        break;
    }
    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE: {
        CMD(EMC_TASK_PLAN_SET_BLOCK_DELETE);
        EMC::TaskPlanSetBlockDelete s(c.state);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_set_block_delete;
        break;
    }
    case EMC_JOINT_HOME_TYPE: {
        CMD(EMC_JOINT_HOME);
        EMC::JointHome s(c.joint);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_home;
        break;
    }
    case EMC_JOINT_UNHOME_TYPE: {
        CMD(EMC_JOINT_UNHOME);
        EMC::JointUnhome s(c.joint);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_unhome;
        break;
    }
    case EMC_JOINT_OVERRIDE_LIMITS_TYPE: {
        CMD(EMC_JOINT_OVERRIDE_LIMITS);
        EMC::JointOverrideLimits s(c.joint);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_override_limits;
        break;
    }
    case EMC_JOINT_SET_BACKLASH_TYPE: {
        CMD(EMC_JOINT_SET_BACKLASH);
        EMC::JointSetBacklash s(c.joint, c.backlash);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_set_backlash;
        break;
    }
    case EMC_JOINT_LOAD_COMP_TYPE: {
        CMD(EMC_JOINT_LOAD_COMP);
        uval = EMC::CreateJointLoadCompDirect(fbb, c.file, c.type).Union();
        utype = EMC::Command::joint_load_comp;
        break;
    }
    case EMC_JOG_CONT_TYPE: {
        CMD(EMC_JOG_CONT);
        EMC::JogCont s(c.joint_or_axis, c.vel, c.jjogmode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::jog_cont;
        break;
    }
    case EMC_JOG_INCR_TYPE: {
        CMD(EMC_JOG_INCR);
        EMC::JogIncr s(c.joint_or_axis, c.incr, c.vel, c.jjogmode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::jog_incr;
        break;
    }
    case EMC_JOG_STOP_TYPE: {
        CMD(EMC_JOG_STOP);
        EMC::JogStop s(c.joint_or_axis, c.jjogmode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::jog_stop;
        break;
    }
    case EMC_TRAJ_SET_SCALE_TYPE: {
        CMD(EMC_TRAJ_SET_SCALE);
        EMC::TrajSetScale s(c.scale);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_scale;
        break;
    }
    case EMC_TRAJ_SET_RAPID_SCALE_TYPE: {
        CMD(EMC_TRAJ_SET_RAPID_SCALE);
        EMC::TrajSetRapidScale s(c.scale);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_rapid_scale;
        break;
    }
    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE: {
        CMD(EMC_TRAJ_SET_SPINDLE_SCALE);
        EMC::TrajSetSpindleScale s(c.spindle, c.scale);
        uval = fbb.CreateStruct(s).Union();
        /* The schema spells it traj_set_spindl_scale. */
        utype = EMC::Command::traj_set_spindl_scale;
        break;
    }
    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE: {
        CMD(EMC_TRAJ_SET_MAX_VELOCITY);
        EMC::TrajSetMaxVelocity s(c.velocity);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_max_velocity;
        break;
    }
    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE: {
        CMD(EMC_TRAJ_SET_TELEOP_ENABLE);
        EMC::TrajSetTeleopEnable s(c.enable);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_teleop_enable;
        break;
    }
    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE: {
        EMC::TrajClearProbeTrippedFlag s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_clear_probe_tripped_flag;
        break;
    }
    case EMC_TRAJ_PROBE_TYPE: {
        CMD(EMC_TRAJ_PROBE);
        EMC::TrajProbe s(from_emcpose(c.pos), c.type, c.vel,
                         c.ini_maxvel, c.acc, c.probe_type);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_probe;
        break;
    }
    case EMC_SPINDLE_ON_TYPE: {
        CMD(EMC_SPINDLE_ON);
        EMC::SpindleOn s(c.spindle, c.speed, c.factor, c.xoffset,
                         c.wait_for_spindle_at_speed);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_on;
        break;
    }
    case EMC_SPINDLE_OFF_TYPE: {
        CMD(EMC_SPINDLE_OFF);
        EMC::SpindleOff s(c.spindle);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_off;
        break;
    }
    case EMC_SPINDLE_INCREASE_TYPE: {
        CMD(EMC_SPINDLE_INCREASE);
        EMC::SpindleIncrease s(c.spindle, c.speed);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_increase;
        break;
    }
    case EMC_SPINDLE_DECREASE_TYPE: {
        CMD(EMC_SPINDLE_DECREASE);
        EMC::SpindleDecrease s(c.spindle, c.speed);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_decrease;
        break;
    }
    case EMC_SPINDLE_CONSTANT_TYPE: {
        CMD(EMC_SPINDLE_CONSTANT);
        EMC::SpindleConstant s(c.spindle, c.speed);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_constant;
        break;
    }
    case EMC_SPINDLE_BRAKE_ENGAGE_TYPE: {
        CMD(EMC_SPINDLE_BRAKE_ENGAGE);
        EMC::SpindleBrakeEngage s(c.spindle);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_brake_engage;
        break;
    }
    case EMC_SPINDLE_BRAKE_RELEASE_TYPE: {
        CMD(EMC_SPINDLE_BRAKE_RELEASE);
        EMC::SpindleBrakeRelease s(c.spindle);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::spindle_brake_release;
        break;
    }
    case EMC_COOLANT_MIST_ON_TYPE: {
        EMC::CoolantMistOn s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::coolant_mist_on;
        break;
    }
    case EMC_COOLANT_MIST_OFF_TYPE: {
        EMC::CoolantMistOff s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::coolant_mist_off;
        break;
    }
    case EMC_COOLANT_FLOOD_ON_TYPE: {
        EMC::CoolantFloodOn s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::coolant_flood_on;
        break;
    }
    case EMC_COOLANT_FLOOD_OFF_TYPE: {
        EMC::CoolantFloodOff s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::coolant_flood_off;
        break;
    }
    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE: {
        CMD(EMC_TOOL_LOAD_TOOL_TABLE);
        uval = EMC::CreateToolLoadToolTableDirect(fbb, c.file).Union();
        utype = EMC::Command::tool_load_tool_table;
        break;
    }
    case EMC_TOOL_SET_OFFSET_TYPE: {
        CMD(EMC_TOOL_SET_OFFSET);
        EMC::ToolSetOffset s(c.pocket, c.toolno, from_emcpose(c.offset),
                             c.diameter, c.frontangle, c.backangle,
                             c.orientation);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::tool_set_offset;
        break;
    }
    case EMC_JOINT_SET_MIN_POSITION_LIMIT_TYPE: {
        CMD(EMC_JOINT_SET_MIN_POSITION_LIMIT);
        EMC::JointSetMinPositionLimit s(c.joint, c.limit);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_set_min_position_limit;
        break;
    }
    case EMC_JOINT_SET_MAX_POSITION_LIMIT_TYPE: {
        CMD(EMC_JOINT_SET_MAX_POSITION_LIMIT);
        EMC::JointSetMaxPositionLimit s(c.joint, c.limit);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::joint_set_max_position_limit;
        break;
    }
    case EMC_TRAJ_SET_MODE_TYPE: {
        CMD(EMC_TRAJ_SET_MODE);
        EMC::TrajSetMode s(static_cast<int>(c.mode));
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_mode;
        break;
    }
    case EMC_TRAJ_SET_FO_ENABLE_TYPE: {
        CMD(EMC_TRAJ_SET_FO_ENABLE);
        EMC::TrajSetFOEnable s(c.mode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_fo_enable;
        break;
    }
    case EMC_TRAJ_SET_SO_ENABLE_TYPE: {
        CMD(EMC_TRAJ_SET_SO_ENABLE);
        EMC::TrajSetSOEnable s(c.spindle, c.mode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_so_enable;
        break;
    }
    case EMC_TRAJ_SET_FH_ENABLE_TYPE: {
        CMD(EMC_TRAJ_SET_FH_ENABLE);
        EMC::TrajSetFHEnable s(c.mode);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::traj_set_fh_enable;
        break;
    }
    case EMC_MOTION_ADAPTIVE_TYPE: {
        CMD(EMC_MOTION_ADAPTIVE);
        EMC::MotionAdaptive s(c.status);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::motion_adaptive;
        break;
    }
    case EMC_MOTION_SET_DOUT_TYPE: {
        CMD(EMC_MOTION_SET_DOUT);
        EMC::MotionSetDOut s(c.index, c.start, c.end, c.now);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::motion_set_dout;
        break;
    }
    case EMC_MOTION_SET_AOUT_TYPE: {
        CMD(EMC_MOTION_SET_AOUT);
        EMC::MotionSetAOut s(c.index, c.start, c.end, c.now);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::motion_set_aout;
        break;
    }
    case EMC_TASK_PLAN_CLOSE_TYPE: {
        EMC::TaskPlanClose s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_close;
        break;
    }
    case EMC_TASK_PLAN_REVERSE_TYPE: {
        EMC::TaskPlanReverse s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_reverse;
        break;
    }
    case EMC_TASK_PLAN_FORWARD_TYPE: {
        EMC::TaskPlanForward s(0);
        uval = fbb.CreateStruct(s).Union();
        utype = EMC::Command::task_plan_forward;
        break;
    }
    case EMC_OPERATOR_ERROR_TYPE: {
        CMD(EMC_OPERATOR_ERROR);
        uval = EMC::CreateSendOperatorErrorDirect(fbb, c.error).Union();
        utype = EMC::Command::send_operator_error;
        break;
    }
    case EMC_OPERATOR_TEXT_TYPE: {
        CMD(EMC_OPERATOR_TEXT);
        uval = EMC::CreateSendOperatorTextDirect(fbb, c.text).Union();
        utype = EMC::Command::send_operator_text;
        break;
    }
    case EMC_OPERATOR_DISPLAY_TYPE: {
        CMD(EMC_OPERATOR_DISPLAY);
        uval = EMC::CreateSendOperatorDisplayDirect(fbb, c.display).Union();
        utype = EMC::Command::send_operator_display;
        break;
    }

    default:
        return false;
    }

#undef CMD

    EMC::CmdChannelMsgBuilder b(fbb);
    b.add_command_type(utype);
    b.add_command(uval);
    b.add_serial(serial);
    fbb.Finish(b.Finish());
    return true;
}

} // namespace

/* ------------------------------------------------------------------ */
/* the client                                                         */
/* ------------------------------------------------------------------ */

struct WsClient::Impl {
    asio::io_context ioc{1};
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws;
    std::thread thread;
    std::atomic<bool> alive{false};

    /* Guards everything the reader thread and the caller share. */
    mutable std::mutex      mu;
    std::condition_variable cv;

    std::vector<uint8_t>          newest_stat;  /* raw ServerMsg */
    unsigned long                 stat_seq = 0; /* bumped per status frame */
    std::map<unsigned long, CmdState> acks;
    std::deque<OperatorMessage>   messages;
    unsigned long                 next_serial = 1;
    unsigned long                 echo_serial = 0;

    /* Reader thread only. */
    beast::flat_buffer rbuf;

    /* Writer state, io_context thread only. */
    std::deque<std::shared_ptr<const std::vector<uint8_t>>> wq;
    bool writing = false;

    /* Caller thread only: the snapshot handed out by status(). */
    EMC_STAT      front;
    bool          have_front = false;
    unsigned long seen_seq = 0;

    std::string last_error;

    void read_loop();
    void on_frame(const uint8_t *data, size_t len);
    void do_write();
};

void WsClient::Impl::on_frame(const uint8_t *data, size_t len)
{
    flatbuffers::Verifier v(data, len);
    if (!EMC::VerifyServerMsgBuffer(v)) return;

    const EMC::ServerMsg *msg = EMC::GetServerMsg(data);

    switch (msg->payload_type()) {
    case EMC::ServerPayload::stat: {
        std::lock_guard<std::mutex> lk(mu);
        /* Only the newest snapshot is worth keeping: it is complete in
           itself, so nothing is lost by skipping the ones in between. */
        newest_stat.assign(data, data + len);
        stat_seq++;
        break;
    }
    case EMC::ServerPayload::ack: {
        const EMC::CmdAck *a = msg->payload_as_ack();
        if (a == nullptr) return;
        CmdState st = CmdState::received;
        switch (a->state()) {
        case EMC::CmdState::refused:  st = CmdState::refused;  break;
        case EMC::CmdState::received: st = CmdState::received; break;
        case EMC::CmdState::done:     st = CmdState::done;     break;
        case EMC::CmdState::error:    st = CmdState::error;    break;
        }
        std::lock_guard<std::mutex> lk(mu);
        acks[a->serial()] = st;
        if (st != CmdState::refused && a->serial() > echo_serial) {
            echo_serial = a->serial();
        }
        while (acks.size() > MAX_ACKS) acks.erase(acks.begin());
        break;
    }
    case EMC::ServerPayload::error: {
        const EMC::ErrorChannelMsg *e = msg->payload_as_error();
        if (e == nullptr) return;
        OperatorMessage om;
        const flatbuffers::String *text = nullptr;
        switch (e->message_type()) {
        case EMC::Message::operator_error:
            om.kind = OperatorMessage::Kind::error;
            if (auto p = e->message_as_operator_error()) text = p->error();
            break;
        case EMC::Message::operator_text:
            om.kind = OperatorMessage::Kind::text;
            if (auto p = e->message_as_operator_text()) text = p->text();
            break;
        case EMC::Message::operator_display:
            om.kind = OperatorMessage::Kind::display;
            if (auto p = e->message_as_operator_display()) text = p->display();
            break;
        default:
            return;
        }
        if (text != nullptr) om.text = text->str();
        std::lock_guard<std::mutex> lk(mu);
        messages.push_back(std::move(om));
        while (messages.size() > MAX_MESSAGES) messages.pop_front();
        break;
    }
    default:
        return;
    }
    cv.notify_all();
}

void WsClient::Impl::read_loop()
{
    ws->async_read(rbuf, [this](beast::error_code ec, std::size_t) {
        if (ec) {
            {
                std::lock_guard<std::mutex> lk(mu);
                alive = false;
                last_error = "read from task: " + ec.message();
            }
            fprintf(stderr, "ws_client: connection to task lost: %s\n",
                    ec.message().c_str());
            cv.notify_all();
            return;
        }
        on_frame(static_cast<const uint8_t *>(rbuf.data().data()), rbuf.size());
        rbuf.consume(rbuf.size());
        read_loop();
    });
}

void WsClient::Impl::do_write()
{
    if (writing || wq.empty()) return;
    writing = true;
    auto buf = wq.front();
    wq.pop_front();
    ws->binary(true);
    ws->async_write(asio::buffer(*buf),
        [this, buf](beast::error_code ec, std::size_t) {
            writing = false;
            if (ec) {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    alive = false;
                    last_error = "write to task: " + ec.message();
                }
                if (ec != net_error_operation_aborted())
                    fprintf(stderr, "ws_client: cannot send to task: %s\n",
                            ec.message().c_str());
                cv.notify_all();
                return;
            }
            do_write();
        });
}

WsClient::WsClient() : d(new Impl) {}

WsClient::~WsClient()
{
    disconnect();
}

int WsClient::connect(const std::string &host, int port, double timeout)
{
    disconnect();

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(timeout > 0.0 ? timeout : 10.0));

    try {
        d->ws.reset(new websocket::stream<beast::tcp_stream>(d->ioc));

        tcp::resolver resolver(d->ioc);
        auto const results = resolver.resolve(host, std::to_string(port));

        /* The handshake is synchronous so that a failure to reach task is
           reported here, not as a silent never-connecting thread. */
        beast::get_lowest_layer(*d->ws).expires_after(std::chrono::seconds(5));
        beast::get_lowest_layer(*d->ws).connect(results);

        d->ws->set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::client));
        d->ws->handshake(host + ":" + std::to_string(port), "/ws");
        d->ws->binary(true);

        /* The deadline above was for getting connected. Leaving it armed
           would tear the connection down five seconds later, whatever was
           going on -- which is exactly what it did the first time. The
           suggested client settings have no idle timeout, and task sends a
           status snapshot every cycle, so a silent connection is already a
           dead one. */
        beast::get_lowest_layer(*d->ws).expires_never();
    } catch (const std::exception &e) {
        d->last_error = std::string("cannot reach task at ") + host + ":"
                      + std::to_string(port) + ": " + e.what();
        d->ws.reset();
        return -1;
    }

    d->alive = true;
    d->read_loop();
    d->thread = std::thread([this]() {
        auto guard = asio::make_work_guard(d->ioc);
        d->ioc.run();
    });

    /* Connected. Deliberately without waiting for a first status frame: a
       caller that needs one calls waitStatus(), and the ones that do not
       should not be held up for a task cycle. It is a small wait, but a UI
       connects while task is still settling and half a millisecond there
       decides whether its first command lands before or after task's own
       start-up work. */
    (void)deadline;
    return 0;
}

int WsClient::waitStatus(double timeout)
{
    const auto start = std::chrono::steady_clock::now();

    for (;;) {
        if (update() != 0) {
            d->last_error = "task closed the connection";
            return -1;
        }
        if (d->have_front) return 0;
        if (timeout > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= timeout) {
            d->last_error = "task accepted the connection but sent no status";
            return -1;
        }
        std::unique_lock<std::mutex> lk(d->mu);
        d->cv.wait_for(lk, std::chrono::milliseconds(5));
    }
}

void WsClient::disconnect()
{
    if (!d->ws && !d->thread.joinable()) return;

    d->alive = false;

    if (d->thread.joinable()) {
        /* Close on the thread that owns the stream, and wait for that to
           have happened. Closing the socket from here while a read is in
           flight leaves beast's websocket state half updated, and it
           asserts on the next read rather than returning an error. */
        std::promise<void> closed;
        auto done = closed.get_future();
        asio::post(d->ioc, [this, &closed]() {
            beast::error_code ec;
            if (d->ws) beast::get_lowest_layer(*d->ws).socket().close(ec);
            closed.set_value();
        });
        done.wait();

        /* The pending read has now failed, so nothing re-arms it; stop the
           context so run() returns despite its work guard. */
        d->ioc.stop();
        d->thread.join();
    } else if (d->ws) {
        beast::error_code ec;
        beast::get_lowest_layer(*d->ws).socket().close(ec);
    }
    d->ws.reset();

    /* Ready to connect again. */
    d->ioc.restart();
    d->rbuf.consume(d->rbuf.size());
    d->wq.clear();
    d->writing = false;
    std::lock_guard<std::mutex> lk(d->mu);
    d->acks.clear();
    d->messages.clear();
    d->echo_serial = 0;
    d->newest_stat.clear();
    d->stat_seq = 0;
    d->seen_seq = 0;
    d->have_front = false;
}

bool WsClient::connected() const
{
    return d->alive;
}

EMC_STAT *WsClient::status() const
{
    return d->have_front ? &d->front : nullptr;
}

bool WsClient::snapshot(EMC_STAT &out) const
{
    std::vector<uint8_t> raw;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        if (d->newest_stat.empty()) return false;
        raw = d->newest_stat;
    }

    flatbuffers::Verifier v(raw.data(), raw.size());
    if (!EMC::VerifyServerMsgBuffer(v)) return false;
    decode_status(EMC::GetServerMsg(raw.data())->payload_as_stat(), out);
    return true;
}

int WsClient::update()
{
    std::vector<uint8_t> raw;
    unsigned long seq = 0;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        if (d->stat_seq != d->seen_seq) {
            raw = d->newest_stat;
            seq = d->stat_seq;
        }
    }

    if (!raw.empty()) {
        flatbuffers::Verifier v(raw.data(), raw.size());
        if (EMC::VerifyServerMsgBuffer(v)) {
            const EMC::ServerMsg *msg = EMC::GetServerMsg(raw.data());
            decode_status(msg->payload_as_stat(), d->front);
            d->have_front = true;
        }
        d->seen_seq = seq;
    }

    return d->alive ? 0 : -1;
}

int WsClient::updateWait(double timeout)
{
    const unsigned long was = d->seen_seq;
    const auto start = std::chrono::steady_clock::now();

    for (;;) {
        if (update() == 0 && d->seen_seq != was) return 0;
        if (!d->alive) return -1;
        if (timeout > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= timeout)
            return -1;
        std::unique_lock<std::mutex> lk(d->mu);
        d->cv.wait_for(lk, std::chrono::milliseconds(20));
    }
}

unsigned long WsClient::send(const RCS_CMD_MSG &cmd)
{
    if (!d->alive) {
        d->last_error = "not connected to task";
        return 0;
    }

    unsigned long serial;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        serial = d->next_serial++;
        /* Recorded before the write so that state() never reports a
           just-sent command as unknown, which would read as "finished". */
        d->acks[serial] = CmdState::pending;
        while (d->acks.size() > MAX_ACKS) d->acks.erase(d->acks.begin());
    }

    flatbuffers::FlatBufferBuilder fbb(1024);
    if (!encode_command(fbb, cmd, serial)) {
        d->last_error = "command type " + std::to_string((int)cmd._type)
                      + " is not carried by the websocket transport yet";
        std::lock_guard<std::mutex> lk(d->mu);
        d->acks[serial] = CmdState::refused;
        return 0;
    }

    auto buf = std::make_shared<std::vector<uint8_t>>(
        fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());

    asio::post(d->ioc, [this, buf]() {
        d->wq.push_back(buf);
        d->do_write();
    });

    return serial;
}

unsigned long WsClient::echoSerial() const
{
    std::lock_guard<std::mutex> lk(d->mu);
    return d->echo_serial;
}

CmdState WsClient::state(unsigned long serial) const
{
    if (serial == 0) return CmdState::unknown;
    std::lock_guard<std::mutex> lk(d->mu);
    auto it = d->acks.find(serial);
    return it == d->acks.end() ? CmdState::unknown : it->second;
}

int WsClient::waitReceived(unsigned long serial, double timeout)
{
    const auto start = std::chrono::steady_clock::now();

    for (;;) {
        update();
        switch (state(serial)) {
        case CmdState::pending:
            break;
        case CmdState::refused:
            return -1;
        default:
            /* received, done, error -- and unknown, which here means the
               ack has already aged out, so it certainly arrived. */
            return 0;
        }
        if (!d->alive) return -1;
        if (timeout > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= timeout)
            return -1;
        std::unique_lock<std::mutex> lk(d->mu);
        d->cv.wait_for(lk, std::chrono::milliseconds(20));
    }
}

int WsClient::waitDone(unsigned long serial, double timeout)
{
    const auto start = std::chrono::steady_clock::now();

    for (;;) {
        update();
        switch (state(serial)) {
        case CmdState::done:
            return 0;
        case CmdState::unknown:
            /* Aged out of the ack table: it finished long ago. The same
               conclusion an NML client drew from serial_diff > 0. */
            return 0;
        case CmdState::error:
        case CmdState::refused:
            return -1;
        default:
            break;
        }
        if (!d->alive) return -1;
        if (timeout > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= timeout)
            return -1;
        std::unique_lock<std::mutex> lk(d->mu);
        d->cv.wait_for(lk, std::chrono::milliseconds(20));
    }
}

bool WsClient::nextMessage(OperatorMessage &out)
{
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->messages.empty()) return false;
    out = std::move(d->messages.front());
    d->messages.pop_front();
    return true;
}

const std::string &WsClient::lastError() const
{
    return d->last_error;
}

} // namespace linuxcnc
