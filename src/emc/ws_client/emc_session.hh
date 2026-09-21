/********************************************************************
* Description: emc_session.hh
*   The three things a user interface talks to task through: a status
*   snapshot, a command channel and an error channel.
*
*   This is where the Python module's behaviour lives. The pybind11 layer
*   on top of it only names things: every check, every conversion and every
*   wait is a C++ method here, so the binding has no logic to get wrong and
*   a C++ caller gets the same interface Python does.
*
*   NML gave these three separate channels because shared memory had no
*   other way to say it. They are three views of one conversation, so here
*   they share one websocket connection to task.
*
* License: GPL Version 2
********************************************************************/
#ifndef EMC_SESSION_HH
#define EMC_SESSION_HH

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
#include <string>
#include <utility>
#include <vector>

#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"
#include "ws_client/ws_client.hh"

namespace linuxcnc {

/* Could not reach task, or task would not take a command. The Python layer
   raises linuxcnc.error for this. */
class SessionError : public std::runtime_error {
public:
    explicit SessionError(const std::string &what) : std::runtime_error(what) {}
};

/* The caller asked for something that does not exist -- a mode that is not
   a mode, a jog function that is not one. ValueError in Python. */
class ArgError : public std::invalid_argument {
public:
    explicit ArgError(const std::string &what) : std::invalid_argument(what) {}
};

/* The connection every stat, command and error_channel in this process
   shares. Connects on first use; throws SessionError if task is not there.

   Where to look comes from LINUXCNC_WS_HOST / LINUXCNC_WS_PORT, else from
   [TASK]WEBSOCKET_PORT in $INI_FILE_NAME, else the built-in default. */
std::shared_ptr<WsClient> sessionClient();

/* Drop the shared connection. The next sessionClient() reconnects. */
void sessionDisconnect();

/* ------------------------------------------------------------------ */

/* One joint, axis or spindle, as the Python API presents it: a mapping of
   names to numbers. Keeping the lookup in C++ means the binding does not
   have to build dictionaries. */
class Fields {
public:
    /* The value is an int, a float or a bool depending on the field, which
       is what the caller has always been given. */
    typedef std::variant<long, double, bool> Value;

    void set(const char *name, Value v) { items_.emplace_back(name, v); }
    Value get(const std::string &name) const;      /* throws ArgError */
    bool has(const std::string &name) const;
    std::vector<std::string> keys() const;
    const std::vector<std::pair<std::string, Value>> &items() const { return items_; }

private:
    std::vector<std::pair<std::string, Value>> items_;
};

/* ------------------------------------------------------------------ */

class Status {
public:
    Status();

    /* Take a new snapshot. Everything read afterwards comes from that one
       snapshot and does not move until the next poll(), which is what the
       NML status buffer never actually promised. */
    void poll();

    const EMC_STAT &s() const { return *reinterpret_cast<const EMC_STAT *>(buf_.data()); }

    /* How far task has got through this process's commands, as of the last
       poll(). It is what a UI has always used echo_serial_number for --
       "has my command been taken yet?" -- and it answers that question
       again now, about this connection rather than about every NML client
       at once. The raw field on EMC_STAT still counts NML commands and is
       no use to a websocket client. */
    long echo_serial_number() const { return static_cast<long>(echo_serial_); }

    /* A live reading, straight from the connection, leaving the polled
       snapshot alone. The backplot logger samples this from its own thread
       with the GIL released. False before the first status arrives. */
    bool live(EMC_STAT &out) const;

    /* Composite attributes. These build something rather than read a
       field, so they are methods; the plain ones are in stat_fields.hh. */
    std::array<double, 9> pose(const EmcPose &p) const;
    std::vector<double> joint_position() const;
    std::vector<double> joint_actual_position() const;
    std::vector<long>   gcodes() const;
    std::vector<long>   mcodes() const;
    std::vector<double> settings() const;
    std::vector<long>   din() const;
    std::vector<long>   dout() const;
    std::vector<double> ain() const;
    std::vector<double> aout() const;
    std::vector<long>   misc_error() const;
    std::vector<long>   homed() const;
    std::vector<long>   limit() const;      /* bit 0..3: min/max hard, min/max soft */
    int                 g5x_index() const;

    std::vector<Fields> joints_detail() const;
    std::vector<Fields> axes_detail() const;
    std::vector<Fields> spindles_detail() const;

private:
    std::shared_ptr<WsClient> client_;
    std::vector<char> buf_;     /* an EMC_STAT; it has no copy constructor */
    unsigned long     echo_serial_ = 0;
    bool              waited_ = false;  /* see poll() */
};

/* ------------------------------------------------------------------ */

/* The argument values command.spindle(), command.jog() and friends take.
   They are the module's own constants, not NML's, and have been since
   before this transport: keep the numbers. */
enum {
    LOCAL_SPINDLE_FORWARD  =  1,
    LOCAL_SPINDLE_REVERSE  = -1,
    LOCAL_SPINDLE_OFF      =  0,
    LOCAL_SPINDLE_INCREASE =  10,
    LOCAL_SPINDLE_DECREASE =  11,
    LOCAL_SPINDLE_CONSTANT =  12,

    LOCAL_MIST_ON  = 1,
    LOCAL_MIST_OFF = 0,

    LOCAL_FLOOD_ON  = 1,
    LOCAL_FLOOD_OFF = 0,

    LOCAL_BRAKE_ENGAGE  = 1,
    LOCAL_BRAKE_RELEASE = 0,

    LOCAL_JOG_STOP       = 0,
    LOCAL_JOG_CONTINUOUS = 1,
    LOCAL_JOG_INCREMENT  = 2,

    LOCAL_AUTO_RUN     = 0,
    LOCAL_AUTO_PAUSE   = 1,
    LOCAL_AUTO_RESUME  = 2,
    LOCAL_AUTO_STEP    = 3,
    LOCAL_AUTO_REVERSE = 4,
    LOCAL_AUTO_FORWARD = 5
};

class Command {
public:
    Command();

    long serial() const { return static_cast<long>(serial_); }

    /* RCS_STATUS as an int: DONE, EXEC or ERROR, and 0 (UNINITIALIZED) if
       the wait timed out. */
    int wait_complete(double timeout);

    void debug(int level);
    void teleop_enable(int enable);
    void traj_mode(int mode);
    void state(int state);
    void mode(int mode);
    void mdi(const std::string &cmd);
    void feedrate(double scale);
    void rapidrate(double scale);
    void maxvel(double velocity);
    void spindleoverride(double scale, int spindle);
    void spindle(int dir, double arg1, double arg2, int arg3);
    void tool_offset(int toolno, double z, double x, double diameter,
                     double frontangle, double backangle, int orientation);
    void mist(int dir);
    void flood(int dir);
    void brake(int dir, int spindle);
    void load_tool_table();
    void abort();
    void task_plan_synch();
    void override_limits();
    void home(int joint);
    void unhome(int joint);
    void jog(int fn, int jjogmode, int ja_value,
             std::optional<double> vel, std::optional<double> incr);
    void reset_interpreter();
    void program_open(const std::string &file);
    void auto_(int fn, std::optional<int> line);
    void set_optional_stop(int state);
    void set_block_delete(int state);
    void set_min_limit(int joint, double limit);
    void set_max_limit(int joint, double limit);
    void set_feed_override(int mode);
    void set_spindle_override(int mode, int spindle);
    void set_feed_hold(int mode);
    void set_adaptive_feed(int status);
    void set_digital_output(int index, int start);
    void set_analog_output(int index, double start);
    void error_msg(const std::string &text);
    void text_msg(const std::string &text);
    void display_msg(const std::string &text);

private:
    /* Send, then wait for task to take it, as this interface has always
       done: a caller that sets a mode and immediately issues a command in
       that mode depends on the first one having landed. */
    void send(const RCS_CMD_MSG &cmd);

    std::shared_ptr<WsClient> client_;
    unsigned long serial_ = 0;
};

/* ------------------------------------------------------------------ */

class ErrorChannel {
public:
    ErrorChannel();

    /* The next operator message as (type, text), where type is one of the
       EMC_OPERATOR_* NML type constants the module exports. Nothing if
       none is waiting. */
    std::optional<std::pair<int, std::string>> poll();

private:
    std::shared_ptr<WsClient> client_;
};

} // namespace linuxcnc

#endif /* EMC_SESSION_HH */
