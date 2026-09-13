#ifndef MMS_DIAGNOSTICS_TRACED_HPP
#define MMS_DIAGNOSTICS_TRACED_HPP

/// \file
/// Traced<Base>: wraps, never replaces, a std:: exception type. Because it
/// publicly inherits Base (e.g. std::invalid_argument), every existing
/// `catch (const std::invalid_argument&)` and
/// `CHECK_THROWS_AS(..., std::invalid_argument)` keeps working unmodified -
/// migrating a throw site from `throw std::invalid_argument(...)` to
/// `throw mms::diagnostics::InvalidArgument(...)` is a purely mechanical,
/// incremental, never-breaking change. The DiagnosticContext (stacktrace,
/// thread id, timestamp, exception type) is captured automatically in the
/// constructor - at the actual throw site, before the stack unwinds - no
/// extra gesture needed beyond using this type instead of the bare std:: one.

#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

#include <diagnostics/diagnostic_context.hpp>

namespace mms::diagnostics {

/// Non-template interface every Traced<Base> instantiation also inherits,
/// so a catch site can dynamic_cast to this regardless of which concrete
/// std:: exception type was actually thrown.
class TracedBase {
public:
    virtual ~TracedBase() = default;

    [[nodiscard]] virtual std::string dump() const = 0;
};

template<class Base> class Traced : public Base, public TracedBase {
public:
    template<class... Args>
    explicit Traced(Args&&... args)
        : Base(std::forward<Args>(args)...),
          context_(capture_diagnostic_context(typeid(Base).name())) {}

    [[nodiscard]] std::string dump() const override { return format_dump(this->what(), context_); }

private:
    DiagnosticContext context_;
};

using InvalidArgument = Traced<std::invalid_argument>;
using RuntimeError = Traced<std::runtime_error>;
using OutOfRange = Traced<std::out_of_range>;

} // namespace mms::diagnostics

#endif // MMS_DIAGNOSTICS_TRACED_HPP