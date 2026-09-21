/********************************************************************
* Description: emc_session.cc
*   Status, Command and ErrorChannel on top of the websocket client.
*   See emc_session.hh for what they are for.
*
* License: GPL Version 2
********************************************************************/

#include "emc_session.hh"
#include "ws_defaults.hh"

#include <inifile.hh>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace linuxcnc {

namespace {

/* How long a command method waits for task to take the command, and how
   long wait_complete() waits by default. Both were five seconds under NML
   and there is no reason for them to change. */
const double EMC_COMMAND_TIMEOUT = 5.0;

/* How long the first poll() waits for task's first status frame. Long
   enough to cover any sane [TASK]CYCLE_TIME, short enough that a program
   loaded from a HAL file -- which task cannot publish to until it is
   ready -- is not held up noticeably. */
const double FIRST_STATUS_TIMEOUT = 0.5;

/* And how often it looks. This is not padding: a command method returning
   as soon as task has taken the command is how fast this interface has
   ever been, and callers are built on it. tests/interp/g28.2/position-model
   homes four joints in a bare loop, which only works because motion has a
   moment to finish each one -- returning in a millisecond instead of ten
   makes motion refuse the fourth. Keep NML's cadence: check first, then
   look again every 10 ms. */
const auto EMC_COMMAND_DELAY = std::chrono::milliseconds(10);

std::shared_ptr<WsClient> g_client;

std::string wsHost()
{
    const char *env = getenv("LINUXCNC_WS_HOST");
    if (env != nullptr && *env != '\0') return std::string(env);
    return std::string(WS_DEFAULT_HOST);
}

int wsPort()
{
    const char *env = getenv("LINUXCNC_WS_PORT");
    if (env != nullptr && *env != '\0') return atoi(env);

    /* The INI file task was started with is the one that says which port
       task is listening on. A UI is given its name the same way task is. */
    const char *ini = getenv("INI_FILE_NAME");
    if (ini != nullptr && *ini != '\0') {
        IniFile inifile(ini);
        if (inifile) {
            return inifile.findSIntV("WEBSOCKET_PORT", "TASK", WS_DEFAULT_PORT);
        }
    }
    return WS_DEFAULT_PORT;
}

} // namespace

std::shared_ptr<WsClient> sessionClient()
{
    if (g_client && g_client->connected()) {
        return g_client;
    }

    auto client = std::make_shared<WsClient>();
    const std::string host = wsHost();
    const int port = wsPort();

    if (port <= 0) {
        throw SessionError("the websocket transport is switched off "
                           "([TASK]WEBSOCKET_PORT = 0), so there is nothing "
                           "to connect to");
    }
    if (client->connect(host, port) != 0) {
        throw SessionError(client->lastError());
    }

    g_client = client;
    return g_client;
}

void sessionDisconnect()
{
    g_client.reset();
}

/* ------------------------------------------------------------------ */

Fields::Value Fields::get(const std::string &name) const
{
    for (const auto &kv : items_) {
        if (kv.first == name) return kv.second;
    }
    throw ArgError("no such field: " + name);
}

bool Fields::has(const std::string &name) const
{
    for (const auto &kv : items_) {
        if (kv.first == name) return true;
    }
    return false;
}

std::vector<std::string> Fields::keys() const
{
    std::vector<std::string> out;
    out.reserve(items_.size());
    for (const auto &kv : items_) out.push_back(kv.first);
    return out;
}

/* ------------------------------------------------------------------ */

Status::Status()
    : client_(sessionClient()), buf_(sizeof(EMC_STAT))
{
    /* An EMC_STAT has to be constructed, not zeroed: EMC_TOOL_STAT and the
       message base class do real work in their constructors.

       No poll() here, as emcmodule's stat() did not poll either: reading a
       field before the first poll() gives the constructed defaults. It
       also keeps constructing a stat off the critical path while a UI is
       starting up alongside task. */
    new (buf_.data()) EMC_STAT();
}

void Status::poll()
{
    if (client_->update() != 0) {
        throw SessionError("lost the connection to task");
    }

    /* connect() returns on the handshake, so the first frame is still in
       flight for a task cycle or so afterwards and there is nothing to
       read yet. Give it a moment rather than hand out the constructed
       defaults: callers size things from what the first poll() reports --
       linuxcnctop builds its per-joint format string from stat.joints at
       import time, then formats slices of that width later, and a stale
       width there is a TypeError rather than a wrong number.

       Bounded, once, and not an error if it expires. A program loaded from
       a HAL file polls before task has reached its main loop, and task
       cannot get there until that program signals ready -- waiting for a
       frame that cannot arrive would deadlock until the timeout. Handing
       back the defaults is also what NML did in that situation: peek() on
       a buffer task had not written yet left the fields alone.

       Not moved into connect(): waiting there costs a task cycle, which is
       what made a UI lose a start-up race against task's first interpreter
       item by half a millisecond. */
    if (!waited_) {
        waited_ = true;
        if (client_->status() == nullptr) {
            client_->waitStatus(FIRST_STATUS_TIMEOUT);
        }
    }
    echo_serial_ = client_->echoSerial();

    const EMC_STAT *live = client_->status();
    if (live != nullptr) {
        /* memcpy rather than assignment: EMC_TOOL_STAT deletes its copy
           constructor, and this is a snapshot, not an object with an
           identity. */
        std::memcpy(buf_.data(), live, sizeof(EMC_STAT));
    }
}

bool Status::live(EMC_STAT &out) const
{
    return client_->snapshot(out);
}

std::array<double, 9> Status::pose(const EmcPose &p) const
{
    return {p.tran.x, p.tran.y, p.tran.z, p.a, p.b, p.c, p.u, p.v, p.w};
}

int Status::g5x_index() const { return s().task.g5x_index; }

std::vector<double> Status::joint_position() const
{
    std::vector<double> v(EMCMOT_MAX_JOINTS);
    for (int i = 0; i < EMCMOT_MAX_JOINTS; i++) v[i] = s().motion.joint[i].output;
    return v;
}

std::vector<double> Status::joint_actual_position() const
{
    std::vector<double> v(EMCMOT_MAX_JOINTS);
    for (int i = 0; i < EMCMOT_MAX_JOINTS; i++) v[i] = s().motion.joint[i].input;
    return v;
}

std::vector<long> Status::gcodes() const
{
    return std::vector<long>(s().task.activeGCodes, s().task.activeGCodes + ACTIVE_G_CODES);
}

std::vector<long> Status::mcodes() const
{
    return std::vector<long>(s().task.activeMCodes, s().task.activeMCodes + ACTIVE_M_CODES);
}

std::vector<double> Status::settings() const
{
    return std::vector<double>(s().task.activeSettings, s().task.activeSettings + ACTIVE_SETTINGS);
}

std::vector<long> Status::din() const
{
    return std::vector<long>(s().motion.synch_di, s().motion.synch_di + EMCMOT_MAX_DIO);
}

std::vector<long> Status::dout() const
{
    return std::vector<long>(s().motion.synch_do, s().motion.synch_do + EMCMOT_MAX_DIO);
}

std::vector<double> Status::ain() const
{
    return std::vector<double>(s().motion.analog_input, s().motion.analog_input + EMCMOT_MAX_AIO);
}

std::vector<double> Status::aout() const
{
    return std::vector<double>(s().motion.analog_output, s().motion.analog_output + EMCMOT_MAX_AIO);
}

std::vector<long> Status::misc_error() const
{
    return std::vector<long>(s().motion.misc_error, s().motion.misc_error + EMCMOT_MAX_MISC_ERROR);
}

std::vector<long> Status::homed() const
{
    std::vector<long> v(EMCMOT_MAX_JOINTS);
    for (int i = 0; i < EMCMOT_MAX_JOINTS; i++) v[i] = s().motion.joint[i].homed;
    return v;
}

std::vector<long> Status::limit() const
{
    std::vector<long> v(EMCMOT_MAX_JOINTS);
    for (int i = 0; i < EMCMOT_MAX_JOINTS; i++) {
        const EMC_JOINT_STAT &j = s().motion.joint[i];
        long b = 0;
        if (j.minHardLimit) b |= 1;
        if (j.maxHardLimit) b |= 2;
        if (j.minSoftLimit) b |= 4;
        if (j.maxSoftLimit) b |= 8;
        v[i] = b;
    }
    return v;
}

std::vector<Fields> Status::joints_detail() const
{
    std::vector<Fields> v(EMCMOT_MAX_JOINTS);
    for (int i = 0; i < EMCMOT_MAX_JOINTS; i++) {
        const EMC_JOINT_STAT &j = s().motion.joint[i];
        Fields &f = v[i];
        f.set("jointType",          (long)j.jointType);
        f.set("units",              j.units);
        f.set("backlash",           j.backlash);
        f.set("min_position_limit", j.minPositionLimit);
        f.set("max_position_limit", j.maxPositionLimit);
        f.set("max_ferror",         j.maxFerror);
        f.set("min_ferror",         j.minFerror);
        f.set("ferror_current",     j.ferrorCurrent);
        f.set("ferror_highmark",    j.ferrorHighMark);
        f.set("output",             j.output);
        f.set("input",              j.input);
        f.set("velocity",           j.velocity);
        f.set("inpos",              (long)j.inpos);
        f.set("homing",             (long)j.homing);
        f.set("homed",              (long)j.homed);
        f.set("fault",              (long)j.fault);
        f.set("enabled",            (long)j.enabled);
        f.set("min_soft_limit",     (long)j.minSoftLimit);
        f.set("max_soft_limit",     (long)j.maxSoftLimit);
        f.set("min_hard_limit",     (long)j.minHardLimit);
        f.set("max_hard_limit",     (long)j.maxHardLimit);
        f.set("override_limits",    (long)j.overrideLimits);
    }
    return v;
}

std::vector<Fields> Status::axes_detail() const
{
    std::vector<Fields> v(EMCMOT_MAX_AXIS);
    for (int i = 0; i < EMCMOT_MAX_AXIS; i++) {
        const EMC_AXIS_STAT &a = s().motion.axis[i];
        Fields &f = v[i];
        f.set("velocity",           a.velocity);
        f.set("min_position_limit", a.minPositionLimit);
        f.set("max_position_limit", a.maxPositionLimit);
    }
    return v;
}

std::vector<Fields> Status::spindles_detail() const
{
    std::vector<Fields> v(EMCMOT_MAX_SPINDLES);
    for (int i = 0; i < EMCMOT_MAX_SPINDLES; i++) {
        const EMC_SPINDLE_STAT &sp = s().motion.spindle[i];
        Fields &f = v[i];
        f.set("brake",             (long)sp.brake);
        f.set("direction",         (long)sp.direction);
        f.set("enabled",           (long)sp.enabled);
        f.set("override_enabled",  (bool)sp.spindle_override_enabled);
        f.set("speed",             sp.speed);
        f.set("override",          sp.spindle_scale);
        f.set("homed",             (bool)sp.homed);
        f.set("orient_state",      (long)sp.orient_state);
        f.set("orient_fault",      (long)sp.orient_fault);
    }
    return v;
}

/* ------------------------------------------------------------------ */

Command::Command() : client_(sessionClient()) {}

void Command::send(const RCS_CMD_MSG &cmd)
{
    unsigned long serial = client_->send(cmd);
    if (serial == 0) {
        throw SessionError(client_->lastError());
    }
    serial_ = serial;

    /* Wait for task to take it. Under NML this interface blocked here too,
       and callers rely on it: setting a mode and immediately issuing a
       command in that mode only works if the mode arrived first. */
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (client_->state(serial) != CmdState::pending) {
            return;
        }
        if (!client_->connected()) {
            throw SessionError("lost the connection to task");
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                >= EMC_COMMAND_TIMEOUT) {
            return;     /* as NML did: give up waiting, do not raise */
        }
        std::this_thread::sleep_for(EMC_COMMAND_DELAY);
    }
}

int Command::wait_complete(double timeout)
{
    if (serial_ == 0) {
        return (int)RCS_STATUS::DONE;   /* nothing sent, nothing to wait for */
    }
    switch (client_->state(serial_)) {
    case CmdState::done:
    case CmdState::unknown:
        return (int)RCS_STATUS::DONE;
    case CmdState::error:
    case CmdState::refused:
        return (int)RCS_STATUS::ERROR;
    default:
        break;
    }
    if (client_->waitDone(serial_, timeout) == 0) {
        return (int)RCS_STATUS::DONE;
    }
    /* Tell a failed command apart from one that simply has not finished:
       the first is a result, the second is a timeout. */
    switch (client_->state(serial_)) {
    case CmdState::error:
    case CmdState::refused:
        return (int)RCS_STATUS::ERROR;
    case CmdState::done:
    case CmdState::unknown:
        return (int)RCS_STATUS::DONE;
    default:
        return (int)RCS_STATUS::UNINITIALIZED;
    }
}

void Command::debug(int level)
{
    EMC_SET_DEBUG m;
    m.debug = level;
    send(m);
}

void Command::teleop_enable(int enable)
{
    EMC_TRAJ_SET_TELEOP_ENABLE m;
    m.enable = enable;
    send(m);
}

void Command::traj_mode(int mode)
{
    EMC_TRAJ_SET_MODE m;
    m.mode = static_cast<EMC_TRAJ_MODE>(mode);
    send(m);
}

void Command::state(int state)
{
    EMC_TASK_SET_STATE m;
    m.state = static_cast<EMC_TASK_STATE>(state);
    switch (m.state) {
    case EMC_TASK_STATE::ESTOP:
    case EMC_TASK_STATE::ESTOP_RESET:
    case EMC_TASK_STATE::ON:
    case EMC_TASK_STATE::OFF:
        break;
    default:
        throw ArgError("Machine state should be STATE_ESTOP, "
                       "STATE_ESTOP_RESET, STATE_ON, or STATE_OFF");
    }
    send(m);
}

void Command::mode(int mode)
{
    EMC_TASK_SET_MODE m;
    m.mode = static_cast<EMC_TASK_MODE>(mode);
    switch (m.mode) {
    case EMC_TASK_MODE::MDI:
    case EMC_TASK_MODE::MANUAL:
    case EMC_TASK_MODE::AUTO:
        break;
    default:
        throw ArgError("Mode should be MODE_MDI, MODE_MANUAL, or MODE_AUTO");
    }
    send(m);
}

void Command::mdi(const std::string &cmd)
{
    EMC_TASK_PLAN_EXECUTE m;
    if (cmd.size() > sizeof(m.command) - 1) {
        throw ArgError("MDI commands limited to " +
                       std::to_string(sizeof(m.command) - 1) + " characters");
    }
    std::strcpy(m.command, cmd.c_str());
    send(m);
}

void Command::feedrate(double scale)
{
    EMC_TRAJ_SET_SCALE m;
    m.scale = scale;
    send(m);
}

void Command::rapidrate(double scale)
{
    EMC_TRAJ_SET_RAPID_SCALE m;
    m.scale = scale;
    send(m);
}

void Command::maxvel(double velocity)
{
    EMC_TRAJ_SET_MAX_VELOCITY m;
    m.velocity = velocity;
    send(m);
}

void Command::spindleoverride(double scale, int spindle)
{
    EMC_TRAJ_SET_SPINDLE_SCALE m;
    m.scale = scale;
    m.spindle = spindle;
    send(m);
}

void Command::spindle(int dir, double arg1, double arg2, int arg3)
{
    switch (dir) {
    case LOCAL_SPINDLE_FORWARD:
    case LOCAL_SPINDLE_REVERSE: {
        EMC_SPINDLE_ON m;
        m.speed = dir * arg1;
        m.spindle = (int)arg2;
        m.wait_for_spindle_at_speed = arg3;
        send(m);
        return;
    }
    case LOCAL_SPINDLE_INCREASE: {
        EMC_SPINDLE_INCREASE m;
        m.spindle = (int)arg1;
        send(m);
        return;
    }
    case LOCAL_SPINDLE_DECREASE: {
        EMC_SPINDLE_DECREASE m;
        m.spindle = (int)arg1;
        send(m);
        return;
    }
    case LOCAL_SPINDLE_CONSTANT: {
        EMC_SPINDLE_CONSTANT m;
        m.spindle = (int)arg1;
        send(m);
        return;
    }
    case LOCAL_SPINDLE_OFF: {
        EMC_SPINDLE_OFF m;
        m.spindle = (int)arg1;
        send(m);
        return;
    }
    default:
        throw ArgError("Spindle direction should be SPINDLE_FORWARD, "
                       "SPINDLE_REVERSE, SPINDLE_OFF, SPINDLE_INCREASE, "
                       "SPINDLE_DECREASE, or SPINDLE_CONSTANT");
    }
}

void Command::tool_offset(int toolno, double z, double x, double diameter,
                          double frontangle, double backangle, int orientation)
{
    EMC_TOOL_SET_OFFSET m;
    m.toolno = toolno;
    m.offset.tran.z = z;
    m.offset.tran.x = x;
    m.diameter = diameter;
    m.frontangle = frontangle;
    m.backangle = backangle;
    m.orientation = orientation;
    send(m);
}

void Command::mist(int dir)
{
    if (dir == LOCAL_MIST_ON)  { EMC_COOLANT_MIST_ON  m; send(m); return; }
    if (dir == LOCAL_MIST_OFF) { EMC_COOLANT_MIST_OFF m; send(m); return; }
    throw ArgError("Mist should be MIST_ON or MIST_OFF");
}

void Command::flood(int dir)
{
    if (dir == LOCAL_FLOOD_ON)  { EMC_COOLANT_FLOOD_ON  m; send(m); return; }
    if (dir == LOCAL_FLOOD_OFF) { EMC_COOLANT_FLOOD_OFF m; send(m); return; }
    throw ArgError("FLOOD should be FLOOD_ON or FLOOD_OFF");
}

void Command::brake(int dir, int spindle)
{
    if (dir == LOCAL_BRAKE_ENGAGE) {
        EMC_SPINDLE_BRAKE_ENGAGE m;
        m.spindle = spindle;
        send(m);
        return;
    }
    if (dir == LOCAL_BRAKE_RELEASE) {
        EMC_SPINDLE_BRAKE_RELEASE m;
        m.spindle = spindle;
        send(m);
        return;
    }
    throw ArgError("BRAKE should be BRAKE_ENGAGE or BRAKE_RELEASE");
}

void Command::load_tool_table()
{
    EMC_TOOL_LOAD_TOOL_TABLE m;
    m.file[0] = '\0';   /* empty means "the one the INI file names" */
    send(m);
}

void Command::abort()
{
    EMC_TASK_ABORT m;
    send(m);
}

void Command::task_plan_synch()
{
    EMC_TASK_PLAN_SYNCH m;
    send(m);
}

void Command::override_limits()
{
    EMC_JOINT_OVERRIDE_LIMITS m;
    m.joint = 0;    /* same number for all */
    send(m);
}

void Command::home(int joint)
{
    EMC_JOINT_HOME m;
    m.joint = joint;
    send(m);
}

void Command::unhome(int joint)
{
    EMC_JOINT_UNHOME m;
    m.joint = joint;
    send(m);
}

void Command::jog(int fn, int jjogmode, int ja_value,
                  std::optional<double> vel, std::optional<double> incr)
{
    if (fn == LOCAL_JOG_STOP) {
        if (vel.has_value() || incr.has_value()) {
            throw ArgError("jog(JOG_STOP, ...) takes 3 arguments");
        }
        EMC_JOG_STOP m;
        m.joint_or_axis = ja_value;
        m.jjogmode = jjogmode;
        send(m);
        return;
    }
    if (fn == LOCAL_JOG_CONTINUOUS) {
        if (!vel.has_value() || incr.has_value()) {
            throw ArgError("jog(JOG_CONTINUOUS, ...) takes 4 arguments");
        }
        EMC_JOG_CONT m;
        m.joint_or_axis = ja_value;
        m.vel = *vel;
        m.jjogmode = jjogmode;
        send(m);
        return;
    }
    if (fn == LOCAL_JOG_INCREMENT) {
        if (!vel.has_value() || !incr.has_value()) {
            throw ArgError("jog(JOG_INCREMENT, ...) takes 5 arguments");
        }
        EMC_JOG_INCR m;
        m.joint_or_axis = ja_value;
        m.vel = *vel;
        m.incr = *incr;
        m.jjogmode = jjogmode;
        send(m);
        return;
    }
    throw ArgError("jog() first argument must be JOG_xxx");
}

void Command::reset_interpreter()
{
    EMC_TASK_PLAN_INIT m;
    send(m);
}

void Command::program_open(const std::string &file)
{
    EMC_TASK_PLAN_CLOSE close;
    send(close);

    EMC_TASK_PLAN_OPEN m;
    if (file.size() > sizeof(m.file) - 1) {
        throw ArgError("File name limited to " +
                       std::to_string(sizeof(m.file) - 1) + " characters");
    }
    std::strcpy(m.file, file.c_str());
    /* NML could ship the file itself in chunks to a task on another
       machine. The websocket transport does not carry that yet, so the
       file has to be readable where task runs -- see notes.md. */
    m.remote_buffersize = 0;
    m.remote_filesize = 0;
    send(m);
}

void Command::auto_(int fn, std::optional<int> line)
{
    switch (fn) {
    case LOCAL_AUTO_RUN: {
        EMC_TASK_PLAN_RUN m;
        m.line = line.value_or(0);
        send(m);
        return;
    }
    case LOCAL_AUTO_PAUSE:   { EMC_TASK_PLAN_PAUSE   m; send(m); return; }
    case LOCAL_AUTO_RESUME:  { EMC_TASK_PLAN_RESUME  m; send(m); return; }
    case LOCAL_AUTO_STEP:    { EMC_TASK_PLAN_STEP    m; send(m); return; }
    case LOCAL_AUTO_REVERSE: { EMC_TASK_PLAN_REVERSE m; send(m); return; }
    case LOCAL_AUTO_FORWARD: { EMC_TASK_PLAN_FORWARD m; send(m); return; }
    default:
        throw SessionError("Unexpected argument '" + std::to_string(fn) +
                           "' to command.auto");
    }
}

void Command::set_optional_stop(int state)
{
    EMC_TASK_PLAN_SET_OPTIONAL_STOP m;
    m.state = state;
    send(m);
}

void Command::set_block_delete(int state)
{
    EMC_TASK_PLAN_SET_BLOCK_DELETE m;
    m.state = state;
    send(m);
}

void Command::set_min_limit(int joint, double limit)
{
    EMC_JOINT_SET_MIN_POSITION_LIMIT m;
    m.joint = joint;
    m.limit = limit;
    send(m);
}

void Command::set_max_limit(int joint, double limit)
{
    EMC_JOINT_SET_MAX_POSITION_LIMIT m;
    m.joint = joint;
    m.limit = limit;
    send(m);
}

void Command::set_feed_override(int mode)
{
    EMC_TRAJ_SET_FO_ENABLE m;
    m.mode = mode;
    send(m);
}

void Command::set_spindle_override(int mode, int spindle)
{
    EMC_TRAJ_SET_SO_ENABLE m;
    m.mode = mode;
    m.spindle = spindle;
    send(m);
}

void Command::set_feed_hold(int mode)
{
    EMC_TRAJ_SET_FH_ENABLE m;
    m.mode = mode;
    send(m);
}

void Command::set_adaptive_feed(int status)
{
    EMC_MOTION_ADAPTIVE m;
    m.status = status;
    send(m);
}

void Command::set_digital_output(int index, int start)
{
    EMC_MOTION_SET_DOUT m;
    m.index = index;
    m.start = start;
    m.now = 1;
    send(m);
}

void Command::set_analog_output(int index, double start)
{
    EMC_MOTION_SET_AOUT m;
    m.index = index;
    m.start = start;
    m.now = 1;
    send(m);
}

void Command::error_msg(const std::string &text)
{
    EMC_OPERATOR_ERROR m;
    std::strncpy(m.error, text.c_str(), LINELEN - 1);
    m.error[LINELEN - 1] = 0;
    send(m);
}

void Command::text_msg(const std::string &text)
{
    EMC_OPERATOR_TEXT m;
    std::strncpy(m.text, text.c_str(), LINELEN - 1);
    m.text[LINELEN - 1] = 0;
    send(m);
}

void Command::display_msg(const std::string &text)
{
    EMC_OPERATOR_DISPLAY m;
    std::strncpy(m.display, text.c_str(), LINELEN - 1);
    m.display[LINELEN - 1] = 0;
    send(m);
}

/* ------------------------------------------------------------------ */

ErrorChannel::ErrorChannel() : client_(sessionClient()) {}

std::optional<std::pair<int, std::string>> ErrorChannel::poll()
{
    if (client_->update() != 0 && !client_->connected()) {
        throw SessionError("lost the connection to task");
    }

    OperatorMessage msg;
    if (!client_->nextMessage(msg)) {
        return std::nullopt;
    }

    int type = EMC_OPERATOR_ERROR_TYPE;
    switch (msg.kind) {
    case OperatorMessage::Kind::error:   type = EMC_OPERATOR_ERROR_TYPE;   break;
    case OperatorMessage::Kind::text:    type = EMC_OPERATOR_TEXT_TYPE;    break;
    case OperatorMessage::Kind::display: type = EMC_OPERATOR_DISPLAY_TYPE; break;
    }
    return std::make_pair(type, msg.text);
}

} // namespace linuxcnc
