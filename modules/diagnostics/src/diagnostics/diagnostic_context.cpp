/// \file
/// Implementation of process_info(), capture_diagnostic_context() and
/// format_dump() (<diagnostics/diagnostic_context.hpp>).

#include <chrono>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <string>
#include <thread>

#include <cpptrace/basic.hpp>
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/utils.hpp>
#include <diagnostics/diagnostic_context.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>

#include <mms/version.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mms::diagnostics {

namespace {

std::uint32_t current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

/// std::thread::id has an operator<< but no fmt::formatter of its own unless
/// <fmt/std.h> is included - and that header is deliberately avoided here:
/// its formatter<std::filesystem::path> is a full (non-template)
/// specialization, so its methods get compiled unconditionally as soon as
/// the header is included, whether or not this TU ever formats a path. On
/// some clang + libstdc++ combinations that specialization itself fails to
/// compile (a real fmt/toolchain incompatibility, not something in this
/// file). Converting through operator<< ourselves sidesteps needing any fmt
/// formatter for std::thread::id at all.
std::string thread_id_to_string(std::thread::id id) {
    std::ostringstream out;
    out << id;
    return out.str();
}

} // namespace

const ProcessInfo& process_info() {
    static const ProcessInfo info{
        .version = mms::version_string,
        .commit_hash = mms::git_commit_hash,
#if defined(NDEBUG)
        .build_type = "Release",
#else
        .build_type = "Debug",
#endif
        .process_id = current_process_id(),
    };
    return info;
}

DiagnosticContext capture_diagnostic_context(std::string_view exception_type_name) {
    return DiagnosticContext{
        .stacktrace = cpptrace::generate_trace(/*skip*/ 1).to_string(),
        .thread_id = std::this_thread::get_id(),
        .captured_at = std::chrono::system_clock::now(),
        .exception_type = cpptrace::demangle(std::string(exception_type_name)),
    };
}

std::string format_dump(std::string_view what, const DiagnosticContext& context) {
    const ProcessInfo& proc = process_info();
    const std::time_t capture_time = std::chrono::system_clock::to_time_t(context.captured_at);

    return fmt::format("--- Error Dump ---\n"
                       "type:      {}\n"
                       "message:   {}\n"
                       "version:   {} ({})\n"
                       "build:     {}\n"
                       "pid:       {}\n"
                       "thread:    {}\n"
                       "time:      {:%Y-%m-%d %H:%M:%S} UTC\n"
                       "stacktrace:\n{}",
                       context.exception_type,
                       what,
                       proc.version,
                       proc.commit_hash,
                       proc.build_type,
                       proc.process_id,
                       thread_id_to_string(context.thread_id),
                       fmt::gmtime(capture_time),
                       context.stacktrace);
}

} // namespace mms::diagnostics