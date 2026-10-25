/// \file
/// Implementation of the workspace (<application/workspace.hpp>).

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <application/workspace.hpp>

namespace mms::application {

namespace {

const std::size_t max_save_name_length = 64;

[[noreturn]] void refuse(const std::string& message) {
    throw std::invalid_argument("save name: " + message);
}

bool allowed(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (std::isalnum(c) != 0 && u < 128) || c == '.' || c == '_' || c == '-';
}

/// The names Windows keeps for devices. A folder called "NUL" cannot be made there, with or
/// without an extension, so no save may carry one: a save must be openable on any machine.
bool is_device_name(std::string_view name) {
    const std::size_t dot = name.find('.');
    std::string stem(name.substr(0, dot));
    std::ranges::transform(stem, stem.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

std::optional<std::string> non_empty(const EnvLookup& env, std::string_view name) {
    std::optional<std::string> value = env(name);
    if (value.has_value() && value->empty()) {
        return std::nullopt;
    }
    return value;
}

void make_folder(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        throw std::runtime_error("Workspace: cannot create " + path.string() + ":" +
                                 error.message());
    }
}

} // namespace

std::filesystem::path default_workspace_root(const EnvLookup& env) {
#if defined(_WIN32)
    if (const auto local = non_empty(env, "LOCALAPPDATA")) {
        return std::filesystem::path(*local) / "mms";
    }
    if (const auto profile = non_empty(env, "USERPROFILE")) {
        return std::filesystem::path(*profile) / "AppData" / "Local" / "mms";
    }
    throw std::runtime_error("Workspace: neither LOCALAPPDATA nor USERPROFILE is set");
#elif defined(__APPLE__)
    if (const auto home = non_empty(env, "HOME")) {
        return std::filesystem::path(*home) / "Library" / "Application Support" / "mms";
    }
    throw std::runtime_error("Workspace: HOME is not set");
#else
    if (const auto data = non_empty(env, "XDG_DATA_HOME")) {
        return std::filesystem::path(*data) / "mms";
    }
    if (const auto home = non_empty(env, "HOME")) {
        return std::filesystem::path(*home) / ".local" / "share" / "mms";
    }
    throw std::runtime_error("Workspace: neither XDG_DATA_HOME nor HOME is set");
#endif
}

std::filesystem::path resolve_workspace_root(const std::optional<std::filesystem::path>& option,
                                             const EnvLookup& env) {
    if (option.has_value()) {
        if (option->empty()) {
            throw std::invalid_argument("Workspace: the workspace option is empty");
        }
        return *option;
    }
    if (const auto home = non_empty(env, "MMS_HOME")) {
        return {*home};
    }
    return default_workspace_root(env);
}

void validate_save_name(std::string_view name) {
    if (name.empty()) {
        refuse("it is empty");
    }
    if (name.size() > max_save_name_length) {
        refuse("it is longer than 64 characters");
    }
    if (!std::ranges::all_of(name, allowed)) {
        refuse("only letters, digits, '.', '_' and '-' are allowed");
    }
    if (name.front() == '.' || name.front() == '-') {
        refuse("it cannot start with '.' or '-'");
    }
    if (name.back() == '.') {
        refuse("it cannot end with '.'");
    }
    if (is_device_name(name)) {
        refuse("it is the name of a Windows device");
    }
}

std::string default_save_name(std::string_view scenario_name,
                              std::chrono::system_clock::time_point when) {
    using namespace std::chrono;
    const sys_days day = floor<days>(when);
    const year_month_day date{day};
    const hh_mm_ss<seconds> time{floor<seconds>(when - day)};

    std::array<char, 20> stamp{};
    static_cast<void>(std::snprintf(stamp.data(),
                                    stamp.size(),
                                    "%04d%02u%02u-%02lld%02lld%02lld",
                                    static_cast<int>(date.year()),
                                    static_cast<unsigned>(date.month()),
                                    static_cast<unsigned>(date.day()),
                                    static_cast<long long>(time.hours().count()),
                                    static_cast<long long>(time.minutes().count()),
                                    static_cast<long long>(time.seconds().count())));
    const std::string suffix = std::string("-") + stamp.data();

    std::string base(scenario_name);
    std::ranges::replace_if(base, [](char c) { return !allowed(c); }, '-');

    // A name cannot start with '.' or '-', nor end with '.'.
    while (!base.empty() && (base.front() == '.' || base.front() == '-')) {
        base.erase(base.begin());
    }
    base.resize(std::min(base.size(), max_save_name_length - suffix.size()));
    while (!base.empty() && base.back() == '.') {
        base.pop_back();
    }
    if (base.empty()) {
        base = "run";
    }

    std::string name = base + suffix;
    // The promise in the header is worth one check.
    validate_save_name(name);
    return name;
}

Workspace Workspace::at(const std::filesystem::path& root) {
    if (root.empty()) {
        throw std::invalid_argument("Workspace: the root is empty");
    }
    return Workspace(root);
}

bool Workspace::exists() const {
    std::error_code error;
    return std::filesystem::is_directory(root_, error);
}

Workspace Workspace::open(const std::filesystem::path& root) {
    Workspace workspace = at(root);
    std::error_code error;
    if (std::filesystem::exists(root, error) && !std::filesystem::is_directory(root, error)) {
        throw std::runtime_error("Workspace: " + root.string() + " exists and is not a folder");
    }
    make_folder(root);
    make_folder(workspace.scenarios_dir());
    make_folder(workspace.saves_dir());
    make_folder(workspace.logs_dir());
    return workspace;
}

std::filesystem::path Workspace::save_dir(std::string_view name) const {
    validate_save_name(name);
    return saves_dir() / std::string(name);
}

} // namespace mms::application