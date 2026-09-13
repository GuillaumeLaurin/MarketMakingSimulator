#ifndef MMS_DIAGNOSTICS_DIAGNOSTIC_CONTEXT_HPP
#define MMS_DIAGNOSTICS_DIAGNOSTIC_CONTEXT_HPP

/// \file
/// DiagnosticContext: everything an error dump needs besides the exception's
/// own message - captured once, at the moment a Traced<Base> is constructed
/// (the actual throw site, before the stack unwinds). capture_diagnostic_context()
/// does the real work (stacktrace via cpptrace, thread/process id, version);
/// format_dump() assembles the final report string. Both are ordinary
/// exported functions, not templates, so Traced<Base> itself (traced.hpp)
/// stays a thin wrapper regardless of how many exception types use it.

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <diagnostics/diagnostics_export.hpp>

namespace mms::diagnostics {

/// Process-wide, unchanging for the life of the process. Computed once,
/// cahed; see process_info()
struct ProcessInfo {
    std::string version;     // mms::version_string
    std::string commit_hash; // mms::git_commit_hash
    std::string build_type;  // "Debug" / "Release" / etc ...
    std::uint32_t process_id;
};

/// The cached ProcessInfo for this process (computed on first call).
[[nodiscard]] MMS_DIAGNOSTICS_EXPORT const ProcessInfo& process_info();

/// Captured fresh at every throw site.
struct DiagnosticContext {
    std::string stacktrace;
    std::thread::id thread_id;
    std::chrono::system_clock::time_point captured_at;
    std::string exception_type;
};

/// Captures the current stacktrace, thread id and timestamp.
/// exception_type_name is typeid(Base).name() from the calling Traced<Base>
/// (demangled where the platform requires it).
[[nodiscard]] MMS_DIAGNOSTICS_EXPORT DiagnosticContext
capture_diagnostic_context(std::string_view exception_type_name);

/// Assembles what() + context + process_info() into the final human-
/// readable dump.
[[nodiscard]] MMS_DIAGNOSTICS_EXPORT std::string format_dump(std::string_view what,
                                                             const DiagnosticContext& context);

} // namespace mms::diagnostics

#endif // MMS_DIAGNOSTICS_DIAGNOSTIC_CONTEXT_HPP