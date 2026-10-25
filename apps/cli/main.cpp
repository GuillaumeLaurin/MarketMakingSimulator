/// \file
/// The `mms` command line. It turns arguments into calls on MMS::application and prints what
/// comes back. It holds no logic of its own: a graphical front end would make the same calls.

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <CLI/CLI.hpp>
#include <application/workspace.hpp>
#include <fmt/core.h>
#include <stdlib.h>

#include <mms/version.hpp>

#include "CLI/CLI.hpp"

namespace {

namespace app = mms::application;

constexpr int exit_ok = 0;
constexpr int exit_failure = 1;
constexpr int exit_usage = 64;

/// The environment of the process.
std::optional<std::string> environment(std::string_view name) {
    const std::string key(name);
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

/// Where --workspace, MMS_HOME or the default says the workspace is. Nothing is created: a
/// command that writes opens the workspace, one that reads only locates it.
app::Workspace locate_workspace(const std::optional<std::string>& option) {
    std::optional<std::filesystem::path> root;
    if (option.has_value()) {
        root = std::filesystem::path(*option);
    }
    return app::Workspace::at(app::resolve_workspace_root(root, environment));
}

/// A path as the operating system writes it, with one kind of separator.
std::string shown(const std::filesystem::path& path) {
    return path.lexically_normal().make_preferred().string();
}

int info(const std::optional<std::string>& option) {
    const app::Workspace workspace = locate_workspace(option);
    fmt::print("workspace  {}{}\n",
               shown(workspace.root()),
               workspace.exists() ? "" : "  (not created yet)");
    fmt::print("settings   {}\n", shown(workspace.settings_file()));
    fmt::print("scenarios  {}\n", shown(workspace.scenarios_dir()));
    fmt::print("saves      {}\n", shown(workspace.saves_dir()));
    fmt::print("logs       {}\n", shown(workspace.logs_dir()));
    return exit_ok;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Market Making Simulator"};
    app.set_version_flag("--version", std::string("mms ") + mms::version_string);
    app.require_subcommand(0, 1);

    std::optional<std::string> workspace_option;
    app.add_option("--workspace",
                   workspace_option,
                   ("The folder that holds the settings, scenarios, saves and logs. "
                    "Defaults to $MMS_HOME, then to a folder of the operating system."));

    CLI::App* info_command =
        app.add_subcommand("info", "Show where the workspace is and what it holds");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error) == 0 ? exit_ok : exit_usage;
    }

    try {
        if (info_command->parsed()) {
            return info(workspace_option);
        }
        fmt::print("{}", app.help());
        return exit_usage;
    } catch (const std::exception& error) {
        fmt::print(stderr, "mms: {}\n", error.what());
        return exit_failure;
    }
}