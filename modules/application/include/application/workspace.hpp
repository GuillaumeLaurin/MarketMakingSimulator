#ifndef MMS_APPLICATION_WORKSPACE_HPP
#define MMS_APPLICATION_WORKSPACE_HPP

/// \file
/// The workspace: the one folder where everything that belongs to an installation lives.
///
///   <root>/
///       settings.toml     the settings of the application
///       scenarios/        scenarios, the models a run starts from
///       saves/<name>/     one folder per save
///       logs/             the log of whatever is not a run
///
/// Nothing the user owns is kept in the source tree. Where the root is comes from, in this
/// order: the --workspace option, the MMS_HOME environment variable, then a folder of the
/// operating system for the data of one user.

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <application/application_export.hpp>

namespace mms::application {

/// Looks up an environment variable. The application passes the real one; a test passes a
/// table, so nothing here depends on the machine the tests run on.
using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

/// The folder for the data of one user: %LOCALAPPDATA%\MMS on Windows, ~/Library/Application
/// Support/MMS on macOS, $XDG_DATA_HOME/mms or ~/.local/share/mms elsewhere. Throws
/// std::runtime_error when the variables that locate the home are missing or empty.
[[nodiscard]] MMS_APPLICATION_EXPORT std::filesystem::path
default_workspace_root(const EnvLookup& env);

/// The root to use: `option` when given, else MMS_HOME when set and not empty, else
/// default_workspace_root. Throws what default_workspace_root throws, and
/// std::invalid_argument for an empty `option`.
[[nodiscard]] MMS_APPLICATION_EXPORT std::filesystem::path
resolve_workspace_root(const std::optional<std::filesystem::path>& option, const EnvLookup&
env);

/// Throws std::invalid_argument unless `name` is fit to be the name of a save folder on
/// every platform: 1 to 64 characters among letters, digits, '.', '_' and '-', not starting
/// with '.' or '-', not ending with '.', and not the name of a Windows device (CON, NUL,
/// COM1 and so on, with or without an extension).
MMS_APPLICATION_EXPORT void validate_save_name(std::string_view name);

/// The name a save gets when the user gives none: the scenario's name, with every character
/// that a save name does not allow replaced by '-', then '-', then the date and the time in
/// UTC as YYYYMMDD-HHMMSS. An empty scenario name gives "run". A long one is cut so that the
/// whole fits in 64 characters. The result always passes validate_save_name.
[[nodiscard]] MMS_APPLICATION_EXPORT std::string
default_save_name(std::string_view scenario_name, std::chrono::system_clock::time_point
when);

class MMS_APPLICATION_EXPORT Workspace {
public:
    /// Opens the workspace at `root`, creating it and its folders when they are not there.
    /// Throws std::runtime_error, naming the path, when a folder cannot be created or when
    /// `root` exists and is not a folder.
    [[nodiscard]] static Workspace open(const std::filesystem::path& root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    [[nodiscard]] std::filesystem::path settings_file() const { return root_ / "settings.toml"; }

    [[nodiscard]] std::filesystem::path scenarios_dir() const { return root_ / "scenarios"; }

    [[nodiscard]] std::filesystem::path saves_dir() const { return root_ / "saves"; }

    [[nodiscard]] std::filesystem::path logs_dir() const { return root_ / "logs"; }

    /// The folder of the save `name`, which may not exist yet. Throws std::invalid_argument
    /// for a name validate_save_name refuses.
    [[nodiscard]] std::filesystem::path save_dir(std::string_view name) const;

private:
    explicit Workspace(std::filesystem::path root) : root_(std::move(root)) {}

    std::filesystem::path root_;
};

} // namespace mms::application

#endif // MMS_APPLICATION_WORKSPACE_HPP