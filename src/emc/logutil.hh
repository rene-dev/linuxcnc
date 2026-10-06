/********************************************************************
 * Description: logutil.hh
 *   Console messages tagged with their severity, replacing libnml's
 *   rcs_print(), rcs_print_error() and rcs_print_debug().
 *
 *   Call sites go through these rather than straight to spdlog, so
 *   each one still says how severe its message is and the EMC_DEBUG_*
 *   gating stays in one place.
 *
 *   The messages go to spdlog's default logger. liblinuxcncini sets it
 *   up when it is loaded (see logutil.cc): named after the program,
 *   info and debug to stdout, warnings and errors to stderr, just the
 *   message. log_configure() then adds time, program and level, and
 *   applies the [EMC]LOG_* settings from the INI file. Messages need no
 *   trailing newline; spdlog ends each line.
 *
 * License: GPL Version 2
 * System: Linux
 ********************************************************************/
#ifndef LINUXCNC_LOGUTIL_HH
#define LINUXCNC_LOGUTIL_HH

#include <utility>
#include <spdlog/spdlog.h>

#include "nml_intf/emcglb.h"	// emc_debug

namespace linuxcnc {

class IniFile;

/// An error, on stderr. Was rcs_print_error().
template <typename... T>
void log_error(fmt::format_string<T...> f, T &&...args)
{
    spdlog::default_logger_raw()->error(f, std::forward<T>(args)...);
}

/// Something unexpected that the program recovers from, on stderr.
template <typename... T>
void log_warn(fmt::format_string<T...> f, T &&...args)
{
    spdlog::default_logger_raw()->warn(f, std::forward<T>(args)...);
}

/// An informational message, on stdout. Was rcs_print().
template <typename... T>
void log_info(fmt::format_string<T...> f, T &&...args)
{
    spdlog::default_logger_raw()->info(f, std::forward<T>(args)...);
}

/// A debug message, on stdout, logged only when any of the EMC_DEBUG_*
/// bits in `flags` is set in emc_debug. Replaces the
/// `if (emc_debug & EMC_DEBUG_x) rcs_print(...)` pattern.
template <typename... T>
void log_debug(unsigned flags, fmt::format_string<T...> f, T &&...args)
{
    if (emc_debug & flags) {
        spdlog::default_logger_raw()->debug(f, std::forward<T>(args)...);
    }
}

/// Replaces the default logger with one named `name`. Done automatically
/// with the program's name when liblinuxcncini is loaded; call it again
/// to pick a different name.
void log_init(const char *name);

/// Switches to the full message pattern and applies [EMC]LOG_LEVEL,
/// LOG_FILE and LOG_PATTERN from the INI file.
void log_configure(const IniFile &inifile);

} // namespace linuxcnc

#endif
