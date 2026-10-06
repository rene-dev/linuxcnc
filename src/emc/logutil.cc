/********************************************************************
 * Description: logutil.cc
 *   Setup of the spdlog default logger that logutil.hh and libnml's
 *   rcs_print() write to.
 *
 * License: GPL Version 2
 * System: Linux
 ********************************************************************/
#include "logutil.hh"

#include <errno.h>	// program_invocation_short_name
#include <memory>
#include <string>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "inifile.hh"

namespace linuxcnc {

namespace {

// Until log_configure() runs, only the message: command line tools and
// libraries such as the INI parser print diagnostics that must read as
// before. The programs of a running LinuxCNC call log_configure() from
// their iniLoad() and switch to the full pattern.
const char *plain_pattern = "%v";
const char *default_pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";

/// Sends warnings and worse to stderr and everything else to stdout, the
/// split rcs_print() and rcs_print_error() had. Colours only on a tty.
class console_sink final : public spdlog::sinks::sink
{
public:
    void log(const spdlog::details::log_msg &msg) override
    {
        if (msg.level >= spdlog::level::warn) {
            err_.log(msg);
        } else {
            out_.log(msg);
        }
    }

    void flush() override
    {
        out_.flush();
        err_.flush();
    }

    void set_pattern(const std::string &pattern) override
    {
        out_.set_pattern(pattern);
        err_.set_pattern(pattern);
    }

    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
    {
        err_.set_formatter(formatter->clone());
        out_.set_formatter(std::move(formatter));
    }

private:
    spdlog::sinks::ansicolor_stdout_sink_mt out_;
    spdlog::sinks::ansicolor_stderr_sink_mt err_;
};

// Install the logger as soon as liblinuxcncini is loaded, so that every
// program linking it logs under its own name without having to ask.
[[maybe_unused]] const bool initialized = (log_init(program_invocation_short_name), true);

} // namespace

void log_init(const char *name)
{
    auto logger = std::make_shared<spdlog::logger>(
        name, std::make_shared<console_sink>());
    logger->set_pattern(plain_pattern);
    // log_debug() already filters on emc_debug, so let its messages through
    logger->set_level(spdlog::level::debug);
    // flush every message: they are rare, and stdout may be a pipe whose
    // reader wants them as they happen
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));
    // SPDLOG_LEVEL=... in the environment overrides the level
    spdlog::cfg::load_env_levels();
}

void log_configure(const IniFile &inifile)
{
    auto logger = spdlog::default_logger();

    // only once: shcom's iniLoad() may run again for another INI file
    static bool have_file;
    auto file = inifile.findString("LOG_FILE", "EMC");
    if (file && !have_file) {
        try {
            // gets its pattern from set_pattern() below
            logger->sinks().push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(*file));
            have_file = true;
        } catch (const spdlog::spdlog_ex &e) {
            log_error("[EMC]LOG_FILE: {}", e.what());
        }
    }

    logger->set_pattern(inifile.findStringV("LOG_PATTERN", "EMC", default_pattern));

    if (auto level = inifile.findString("LOG_LEVEL", "EMC")) {
        auto l = spdlog::level::from_str(*level);
        // from_str() maps anything it does not know to "off"
        if (l == spdlog::level::off && *level != "off") {
            log_error("[EMC]LOG_LEVEL: unknown level '{}', expected one of "
                      "trace, debug, info, warning, error, critical, off",
                      *level);
        } else {
            logger->set_level(l);
        }
    }

    // and SPDLOG_LEVEL in the environment still has the last word
    spdlog::cfg::load_env_levels();
}

} // namespace linuxcnc
