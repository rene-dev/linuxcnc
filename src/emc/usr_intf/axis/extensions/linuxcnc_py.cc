/********************************************************************
* Description: linuxcnc_py.cc
*   The `linuxcnc` Python module.
*
*   There is deliberately almost no logic in this file. Everything the
*   module does -- connecting, polling, validating arguments, waiting for a
*   command -- is a C++ method in emc/ws_client/emc_session.{cc,hh}, and the
*   ninety flat attributes of `stat` are a table in stat_fields.hh. What is
*   left here is names: which C++ thing each Python name refers to.
*
*   That split is the point. A C++ caller gets the same interface Python
*   does, and a bug in the machine behaviour can be found without reading
*   any Python glue.
*
*   Not ported yet, and still C API in emcmodule.cc: the INI file reader and
*   the AXIS backplot (positionlogger and the vertex/offset helpers). They
*   are registered into this module by emcRegisterLegacy(), so `linuxcnc`
*   is one module with one name as it has always been.
*
* License: GPL Version 2
********************************************************************/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "config.h"
#include "rcs_status.hh"
#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"
#include "nml_intf/debugflags.h"
#include "nml_msg.hh"                // NML_ERROR_TYPE and friends, still exported
#include <kinematics.h>
#include "tooldata/tooldata.hh"

#include "ws_client/emc_session.hh"
#include "ws_client/stat_fields.hh"
#include "emcmodule.hh"

namespace py = pybind11;
using namespace linuxcnc;

namespace {

/* The tool table is read straight from the tooldata mmap, not from task,
   so it needs one initialisation before the first read. It used to hang off
   the first stat.poll(); keeping it here keeps that out of Status, which
   has nothing to do with tool data. */
bool tooldataReady()
{
    static bool tried = false;
    static bool ok = false;
    if (!tried) {
        tried = true;
        ok = (tool_mmap_user() == 0);
        if (!ok) {
            fprintf(stderr, "linuxcnc: continuing without tool mmap data\n");
        }
    }
    return ok;
}

/* One tool table entry. The names and their order are the API. */
struct ToolResult {
    long   id;
    double xoffset, yoffset, zoffset;
    double aoffset, boffset, coffset;
    double uoffset, voffset, woffset;
    double diameter, frontangle, backangle;
    long   orientation;

    py::object item(Py_ssize_t i) const
    {
        switch (i) {
        case 0:  return py::cast(id);
        case 1:  return py::cast(xoffset);
        case 2:  return py::cast(yoffset);
        case 3:  return py::cast(zoffset);
        case 4:  return py::cast(aoffset);
        case 5:  return py::cast(boffset);
        case 6:  return py::cast(coffset);
        case 7:  return py::cast(uoffset);
        case 8:  return py::cast(voffset);
        case 9:  return py::cast(woffset);
        case 10: return py::cast(diameter);
        case 11: return py::cast(frontangle);
        case 12: return py::cast(backangle);
        case 13: return py::cast(orientation);
        default: throw py::index_error("tool index out of range");
        }
    }
};

ToolResult toolResult(const CANON_TOOL_TABLE &t)
{
    ToolResult r;
    r.id = t.toolno;
    r.xoffset = t.offset.tran.x;
    r.yoffset = t.offset.tran.y;
    r.zoffset = t.offset.tran.z;
    r.aoffset = t.offset.a;
    r.boffset = t.offset.b;
    r.coffset = t.offset.c;
    r.uoffset = t.offset.u;
    r.voffset = t.offset.v;
    r.woffset = t.offset.w;
    r.diameter = t.diameter;
    r.frontangle = t.frontangle;
    r.backangle = t.backangle;
    r.orientation = t.orientation;
    return r;
}

std::vector<ToolResult> toolTable()
{
    std::vector<ToolResult> out;
    if (!tooldataReady()) return out;

    const int idxmax = tooldata_last_index_get() + 1;
    out.reserve(idxmax);
    for (int idx = 0; idx < idxmax; idx++) {
        CANON_TOOL_TABLE t;
        if (tooldata_get(&t, idx) != IDX_OK) continue;
        out.push_back(toolResult(t));
    }
    return out;
}

py::dict toolInfo(int toolno)
{
    if (!tooldataReady()) {
        throw py::value_error("toolinfo: no tool data available");
    }
    /* Tool 0 is refused because it means two different things depending on
       the toolchanger (see docs/code/code-notes.adoc); the tool in the
       spindle is stat.tool_table[0]. */
    if (toolno == 0) {
        throw py::value_error("toolinfo: for tool in spindle: "
                              "use linuxcnc.stat.tool_table[0]");
    }

    CANON_TOOL_TABLE t = tooldata_entry_init();
    const int idx = tooldata_find_index_for_tool(toolno);
    if (tooldata_get(&t, idx) != IDX_OK) {
        throw py::value_error("toolinfo: NO tooldata for toolno=" +
                              std::to_string(toolno));
    }

    py::dict d;
    d["toolno"]      = t.toolno;
    d["pocketno"]    = t.pocketno;
    d["diameter"]    = t.diameter;
    d["frontangle"]  = t.frontangle;
    d["backangle"]   = t.backangle;
    d["orientation"] = t.orientation;
    d["xoffset"]     = t.offset.tran.x;
    d["yoffset"]     = t.offset.tran.y;
    d["zoffset"]     = t.offset.tran.z;
    d["aoffset"]     = t.offset.a;
    d["boffset"]     = t.offset.b;
    d["coffset"]     = t.offset.c;
    d["uoffset"]     = t.offset.u;
    d["voffset"]     = t.offset.v;
    d["woffset"]     = t.offset.w;
    d["comment"]     = std::string(t.comment);
    return d;
}

/* Every sequence `stat` hands out is a tuple, and callers compare against
   tuple literals, so a list will not do however much it looks the same. */
template <class C>
py::tuple toTuple(const C &v)
{
    py::tuple t(v.size());
    for (size_t i = 0; i < v.size(); i++) t[i] = py::cast(v[i]);
    return t;
}

/* joint, axis and spindle have always been tuples of plain dicts, so that
   is what they stay. What goes in them is decided in Fields, in C++. */
py::tuple fieldsTuple(const std::vector<Fields> &v)
{
    py::tuple out(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        py::dict d;
        for (const auto &kv : v[i].items()) {
            std::visit([&](auto value) { d[kv.first.c_str()] = value; }, kv.second);
        }
        out[i] = d;
    }
    return out;
}

} // namespace

PYBIND11_MODULE(linuxcnc, m)
{
    m.doc() = "Interface to LinuxCNC";

    static py::exception<SessionError> error(m, "error", PyExc_RuntimeError);
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) std::rethrow_exception(p);
        } catch (const ArgError &e) {
            /* A bad argument is the caller's mistake, not a machine fault. */
            PyErr_SetString(PyExc_ValueError, e.what());
        } catch (const SessionError &e) {
            py::set_error(error, e.what());
        }
    });

    /* ---------------------------------------------------------------- */
    /* stat                                                             */
    /* ---------------------------------------------------------------- */

    py::class_<ToolResult>(m, "tool")
        .def_readonly("id",          &ToolResult::id)
        .def_readonly("xoffset",     &ToolResult::xoffset)
        .def_readonly("yoffset",     &ToolResult::yoffset)
        .def_readonly("zoffset",     &ToolResult::zoffset)
        .def_readonly("aoffset",     &ToolResult::aoffset)
        .def_readonly("boffset",     &ToolResult::boffset)
        .def_readonly("coffset",     &ToolResult::coffset)
        .def_readonly("uoffset",     &ToolResult::uoffset)
        .def_readonly("voffset",     &ToolResult::voffset)
        .def_readonly("woffset",     &ToolResult::woffset)
        .def_readonly("diameter",    &ToolResult::diameter)
        .def_readonly("frontangle",  &ToolResult::frontangle)
        .def_readonly("backangle",   &ToolResult::backangle)
        .def_readonly("orientation", &ToolResult::orientation)
        .def("__len__", [](const ToolResult &) { return 14; })
        .def("__getitem__", &ToolResult::item);

    auto stat = py::class_<Status>(m, "stat")
        .def(py::init<>())
        .def("poll", &Status::poll, "Update current machine state")
        .def("toolinfo", [](Status &, int toolno) { return toolInfo(toolno); },
             py::arg("toolnumber"),
             "toolinfo(toolnumber):\n"
             "   returns dict for toolnumber parameters (pocket,offsets,etc)\n"
             "   ValueError Exception if toolnumber not available");

/* The flat attributes, straight off the tables in stat_fields.hh. */
#define BIND_INT(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return (long)s.s().path; });
#define BIND_BOOL(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return (bool)s.s().path; });
#define BIND_DOUBLE(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return (double)s.s().path; });
#define BIND_STRING(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return std::string(s.s().path); });
#define BIND_U64(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return (uint64_t)s.s().path; });
#define BIND_POSE(name, path) \
    stat.def_property_readonly(#name, [](const Status &s) { return toTuple(s.pose(s.s().path)); });

    STAT_INT_FIELDS(BIND_INT)
    STAT_BOOL_FIELDS(BIND_BOOL)
    STAT_DOUBLE_FIELDS(BIND_DOUBLE)
    STAT_STRING_FIELDS(BIND_STRING)
    STAT_U64_FIELDS(BIND_U64)
    STAT_POSE_FIELDS(BIND_POSE)

#undef BIND_INT
#undef BIND_BOOL
#undef BIND_DOUBLE
#undef BIND_STRING
#undef BIND_U64
#undef BIND_POSE

#define BIND_SEQ(name, call) \
    stat.def_property_readonly(#name, [](const Status &s) { return toTuple(s.call()); });

    BIND_SEQ(joint_position,        joint_position)
    BIND_SEQ(joint_actual_position, joint_actual_position)
    BIND_SEQ(gcodes,                gcodes)
    BIND_SEQ(mcodes,                mcodes)
    BIND_SEQ(din,                   din)
    BIND_SEQ(dout,                  dout)
    BIND_SEQ(ain,                   ain)
    BIND_SEQ(aout,                  aout)
    BIND_SEQ(misc_error,            misc_error)
    BIND_SEQ(limit,                 limit)

#undef BIND_SEQ

    stat.def_property_readonly("echo_serial_number", &Status::echo_serial_number)
        .def_property_readonly("g5x_index", &Status::g5x_index)
        .def_property_readonly("settings", [](const Status &s) { return toTuple(s.settings()); },
            "This is an array containing the Interp active settings: sequence number,\n"
            "feed rate, spindle speed, and G64 blend and naive CAM tolerances.")
        .def_property_readonly("homed", [](const Status &s) { return toTuple(s.homed()); },
            "An array of integers indicating the 'homed' status of each joint (0 or 1).")
        .def_property_readonly("joint",   [](const Status &s) { return fieldsTuple(s.joints_detail()); })
        .def_property_readonly("axis",    [](const Status &s) { return fieldsTuple(s.axes_detail()); })
        .def_property_readonly("spindle", [](const Status &s) { return fieldsTuple(s.spindles_detail()); })
        .def_property_readonly("tool_table", [](const Status &) { return toTuple(toolTable()); },
            "The tooltable, expressed as a list of tools.  Each tool is a dict with the\n"
            "tool id (tool number), diameter, offsets, etc.");

    /* ---------------------------------------------------------------- */
    /* command                                                          */
    /* ---------------------------------------------------------------- */

    py::class_<Command>(m, "command")
        .def(py::init<>())
        .def_property_readonly("serial", &Command::serial)
        .def("wait_complete", &Command::wait_complete, py::arg("timeout") = 5.0)
        .def("debug", &Command::debug)
        .def("teleop_enable", &Command::teleop_enable)
        .def("traj_mode", &Command::traj_mode)
        .def("state", &Command::state,
            "state(NEW_STATE) - Set the machine E-Stop & Power-On state.\n"
            "Possible values for `NEW_STATE` are:\n"
            "    STATE_ESTOP: Power off and enter E-Stop mode.\n"
            "    STATE_ESTOP_RESET: Reset (leave) E-Stop mode, but remain powered off.\n"
            "    STATE_ON: Power on (only works from STATE_ESTOP_RESET state).\n"
            "    STATE_OFF: Power off (only works from STATE_ON state).\n")
        .def("mode", &Command::mode)
        .def("mdi", &Command::mdi)
        .def("feedrate", &Command::feedrate)
        .def("rapidrate", &Command::rapidrate)
        .def("maxvel", &Command::maxvel)
        .def("spindleoverride", &Command::spindleoverride,
             py::arg("scale"), py::arg("spindle") = 0)
        .def("spindle", &Command::spindle,
             py::arg("dir"), py::arg("arg1") = 0.0, py::arg("arg2") = 0.0,
             py::arg("arg3") = 0)
        .def("tool_offset", &Command::tool_offset)
        .def("mist", &Command::mist)
        .def("flood", &Command::flood)
        .def("brake", &Command::brake, py::arg("dir"), py::arg("spindle") = 0)
        .def("load_tool_table", &Command::load_tool_table)
        .def("abort", &Command::abort)
        .def("task_plan_synch", &Command::task_plan_synch)
        .def("override_limits", &Command::override_limits)
        .def("home", &Command::home,
            "home(JOINT) - Home the specified joint.\n"
            "JOINT can be a valid joint number (0-9), or -1 to home all joints.\n")
        .def("unhome", &Command::unhome)
        .def("jog", &Command::jog,
             py::arg("fn"), py::arg("jjogmode"), py::arg("ja_value"),
             py::arg("vel") = py::none(), py::arg("incr") = py::none(),
            "jog(JOG_CONTINUOUS, joint_flag, index, speed)\n"
            "jog(JOG_INCREMENT, joint_flag, index, speed, increment)\n"
            "jog(JOG_STOP, joint_flag, index)\n"
            "\n"
            "Start or stop a continuous or incremental jog of a joint or an axis.\n"
            "\n"
            "    joint_flag: True to jog a joint, False to jog an axis\n"
            "    index: the index of the joint or axis to jog\n"
            "    speed: jog speed\n"
            "    increment: distance to jog\n")
        .def("reset_interpreter", &Command::reset_interpreter)
        .def("program_open", &Command::program_open)
        .def("auto", &Command::auto_, py::arg("fn"), py::arg("line") = py::none())
        .def("set_optional_stop", &Command::set_optional_stop)
        .def("set_block_delete", &Command::set_block_delete)
        .def("set_min_limit", &Command::set_min_limit)
        .def("set_max_limit", &Command::set_max_limit)
        .def("set_feed_override", &Command::set_feed_override)
        .def("set_spindle_override", &Command::set_spindle_override,
             py::arg("mode"), py::arg("spindle") = 0)
        .def("set_feed_hold", &Command::set_feed_hold)
        .def("set_adaptive_feed", &Command::set_adaptive_feed)
        .def("set_digital_output", &Command::set_digital_output)
        .def("set_analog_output", &Command::set_analog_output)
        .def("error_msg", &Command::error_msg, "Send operator error message")
        .def("text_msg", &Command::text_msg, "Send operator text message")
        .def("display_msg", &Command::display_msg, "Send operator display message");

    /* ---------------------------------------------------------------- */
    /* error_channel                                                    */
    /* ---------------------------------------------------------------- */

    py::class_<ErrorChannel>(m, "error_channel")
        .def(py::init<>())
        .def("poll", [](ErrorChannel &e) -> py::object {
                auto msg = e.poll();
                if (!msg) return py::none();
                return py::make_tuple(msg->first, msg->second);
            }, "Poll for errors");

    /* ---------------------------------------------------------------- */
    /* constants                                                        */
    /* ---------------------------------------------------------------- */

    m.attr("PREFIX")  = EMC2_HOME;
    m.attr("SHARE")   = EMC2_HOME "/share";
    m.attr("version") = PACKAGE_VERSION;

    m.attr("OPERATOR_ERROR")   = (int)EMC_OPERATOR_ERROR_TYPE;
    m.attr("OPERATOR_TEXT")    = (int)EMC_OPERATOR_TEXT_TYPE;
    m.attr("OPERATOR_DISPLAY") = (int)EMC_OPERATOR_DISPLAY_TYPE;
    m.attr("NML_ERROR")        = (int)NML_ERROR_TYPE;
    m.attr("NML_TEXT")         = (int)NML_TEXT_TYPE;
    m.attr("NML_DISPLAY")      = (int)NML_DISPLAY_TYPE;

    m.attr("LINEAR")  = (int)EMC_LINEAR;
    m.attr("ANGULAR") = (int)EMC_ANGULAR;

    m.attr("INTERP_IDLE")    = (int)EMC_TASK_INTERP::IDLE;
    m.attr("INTERP_READING") = (int)EMC_TASK_INTERP::READING;
    m.attr("INTERP_PAUSED")  = (int)EMC_TASK_INTERP::PAUSED;
    m.attr("INTERP_WAITING") = (int)EMC_TASK_INTERP::WAITING;

    m.attr("MODE_MDI")    = (int)EMC_TASK_MODE::MDI;
    m.attr("MODE_MANUAL") = (int)EMC_TASK_MODE::MANUAL;
    m.attr("MODE_AUTO")   = (int)EMC_TASK_MODE::AUTO;

    m.attr("STATE_OFF")         = (int)EMC_TASK_STATE::OFF;
    m.attr("STATE_ON")          = (int)EMC_TASK_STATE::ON;
    m.attr("STATE_ESTOP")       = (int)EMC_TASK_STATE::ESTOP;
    m.attr("STATE_ESTOP_RESET") = (int)EMC_TASK_STATE::ESTOP_RESET;

    m.attr("SPINDLE_FORWARD")  = (int)LOCAL_SPINDLE_FORWARD;
    m.attr("SPINDLE_REVERSE")  = (int)LOCAL_SPINDLE_REVERSE;
    m.attr("SPINDLE_OFF")      = (int)LOCAL_SPINDLE_OFF;
    m.attr("SPINDLE_INCREASE") = (int)LOCAL_SPINDLE_INCREASE;
    m.attr("SPINDLE_DECREASE") = (int)LOCAL_SPINDLE_DECREASE;
    m.attr("SPINDLE_CONSTANT") = (int)LOCAL_SPINDLE_CONSTANT;

    m.attr("MIST_ON")  = (int)LOCAL_MIST_ON;
    m.attr("MIST_OFF") = (int)LOCAL_MIST_OFF;

    m.attr("FLOOD_ON")  = (int)LOCAL_FLOOD_ON;
    m.attr("FLOOD_OFF") = (int)LOCAL_FLOOD_OFF;

    m.attr("BRAKE_ENGAGE")  = (int)LOCAL_BRAKE_ENGAGE;
    m.attr("BRAKE_RELEASE") = (int)LOCAL_BRAKE_RELEASE;

    m.attr("JOG_STOP")       = (int)LOCAL_JOG_STOP;
    m.attr("JOG_CONTINUOUS") = (int)LOCAL_JOG_CONTINUOUS;
    m.attr("JOG_INCREMENT")  = (int)LOCAL_JOG_INCREMENT;

    m.attr("AUTO_RUN")     = (int)LOCAL_AUTO_RUN;
    m.attr("AUTO_PAUSE")   = (int)LOCAL_AUTO_PAUSE;
    m.attr("AUTO_RESUME")  = (int)LOCAL_AUTO_RESUME;
    m.attr("AUTO_STEP")    = (int)LOCAL_AUTO_STEP;
    m.attr("AUTO_REVERSE") = (int)LOCAL_AUTO_REVERSE;
    m.attr("AUTO_FORWARD") = (int)LOCAL_AUTO_FORWARD;

    m.attr("TRAJ_MODE_FREE")   = (int)EMC_TRAJ_MODE::FREE;
    m.attr("TRAJ_MODE_COORD")  = (int)EMC_TRAJ_MODE::COORD;
    m.attr("TRAJ_MODE_TELEOP") = (int)EMC_TRAJ_MODE::TELEOP;

    m.attr("MOTION_TYPE_TRAVERSE")    = (int)EMC_MOTION_TYPE_TRAVERSE;
    m.attr("MOTION_TYPE_FEED")        = (int)EMC_MOTION_TYPE_FEED;
    m.attr("MOTION_TYPE_ARC")         = (int)EMC_MOTION_TYPE_ARC;
    m.attr("MOTION_TYPE_TOOLCHANGE")  = (int)EMC_MOTION_TYPE_TOOLCHANGE;
    m.attr("MOTION_TYPE_PROBING")     = (int)EMC_MOTION_TYPE_PROBING;
    m.attr("MOTION_TYPE_INDEXROTARY") = (int)EMC_MOTION_TYPE_INDEXROTARY;

    m.attr("KINEMATICS_IDENTITY")     = (int)KINEMATICS_IDENTITY;
    m.attr("KINEMATICS_FORWARD_ONLY") = (int)KINEMATICS_FORWARD_ONLY;
    m.attr("KINEMATICS_INVERSE_ONLY") = (int)KINEMATICS_INVERSE_ONLY;
    m.attr("KINEMATICS_BOTH")         = (int)KINEMATICS_BOTH;

    m.attr("DEBUG_CONFIG")      = (int)EMC_DEBUG_CONFIG;
    m.attr("DEBUG_VERSIONS")    = (int)EMC_DEBUG_VERSIONS;
    m.attr("DEBUG_TASK_ISSUE")  = (int)EMC_DEBUG_TASK_ISSUE;
    m.attr("DEBUG_MOTION_TIME") = (int)EMC_DEBUG_MOTION_TIME;
    m.attr("DEBUG_INTERP")      = (int)EMC_DEBUG_INTERP;
    m.attr("DEBUG_RCS")         = (int)EMC_DEBUG_RCS;
    m.attr("DEBUG_INTERP_LIST") = (int)EMC_DEBUG_INTERP_LIST;
    m.attr("DEBUG_OWORD")       = (int)EMC_DEBUG_OWORD;
    m.attr("DEBUG_REMAP")       = (int)EMC_DEBUG_REMAP;
    m.attr("DEBUG_PYTHON")      = (int)EMC_DEBUG_PYTHON;
    m.attr("DEBUG_STATE_TAGS")  = (int)EMC_DEBUG_STATE_TAGS;

    m.attr("EXEC_ERROR")                        = (int)EMC_TASK_EXEC::ERROR;
    m.attr("EXEC_DONE")                         = (int)EMC_TASK_EXEC::DONE;
    m.attr("EXEC_WAITING_FOR_MOTION")           = (int)EMC_TASK_EXEC::WAITING_FOR_MOTION;
    m.attr("EXEC_WAITING_FOR_MOTION_QUEUE")     = (int)EMC_TASK_EXEC::WAITING_FOR_MOTION_QUEUE;
    m.attr("EXEC_WAITING_FOR_IO")               = (int)EMC_TASK_EXEC::WAITING_FOR_IO;
    m.attr("EXEC_WAITING_FOR_MOTION_AND_IO")    = (int)EMC_TASK_EXEC::WAITING_FOR_MOTION_AND_IO;
    m.attr("EXEC_WAITING_FOR_DELAY")            = (int)EMC_TASK_EXEC::WAITING_FOR_DELAY;
    m.attr("EXEC_WAITING_FOR_SYSTEM_CMD")       = (int)EMC_TASK_EXEC::WAITING_FOR_SYSTEM_CMD;
    m.attr("EXEC_WAITING_FOR_SPINDLE_ORIENTED") = (int)EMC_TASK_EXEC::WAITING_FOR_SPINDLE_ORIENTED;

    m.attr("MAX_JOINTS") = (int)EMCMOT_MAX_JOINTS;
    m.attr("MAX_AXIS")   = (int)EMCMOT_MAX_AXIS;

    m.attr("RCS_DONE")  = (int)RCS_STATUS::DONE;
    m.attr("RCS_EXEC")  = (int)RCS_STATUS::EXEC;
    m.attr("RCS_ERROR") = (int)RCS_STATUS::ERROR;

    /* The INI reader and the AXIS backplot, still on the C API. */
    emcRegisterLegacy(m.ptr(), error.ptr());
}
