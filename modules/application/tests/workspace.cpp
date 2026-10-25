/// \file
/// Tests for application/workspace.hpp: where the root comes from and in which order, what
/// a save may be called, the default name of a save, and the folders a workspace creates.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include <application/workspace.hpp>
#include <catch2/catch_test_macros.hpp>

namespace app = mms::application;
namespace fs = std::filesystem;

namespace {

/// An environment made of a table.
app::EnvLookup env_of(std::map<std::string, std::string> table) {
    return [table = std::move(table)](std::string_view name) -> std::optional<std::string> {
        const auto found = table.find(std::string(name));
        if (found == table.end()) {
            return std::nullopt;
        }
        return found->second;
    };
}

/// A folder of its own under the temporary one, gone when the test is over.
struct TempFolder {
    TempFolder() {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("mms-test-workspace-" + std::to_string(++counter) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::remove_all(path);
    }

    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    fs::path path;
};

std::chrono::system_clock::time_point
at(int year, unsigned month, unsigned day, int hour, int minute, int second) {
    using namespace std::chrono;
    return sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}} +
           hours{hour} + minutes{minute} + seconds{second};
}

} // namespace

// ------------------------------------------------------------------ the root

TEST_CASE("The option comes first, then MMS_HOME, then the default", "[application][workspace]") {
    const auto env =
        env_of({{"MMS_HOME", "/from/home"}, {"LOCALAPPDATA", "/local"}, {"HOME", "/h"}});

    CHECK(app::resolve_workspace_root(fs::path("/from/option"), env) == fs::path("/from/option"));
    CHECK(app::resolve_workspace_root(std::nullopt, env) == fs::path("/from/home"));

    const auto bare = env_of({{"LOCALAPPDATA", "/local"}, {"HOME", "/h"}, {"USERPROFILE", "/u"}});
    CHECK(app::resolve_workspace_root(std::nullopt, bare) == app::default_workspace_root(bare));
}

TEST_CASE("An empty MMS_HOME counts as not set", "[application][workspace]") {
    const auto env = env_of({{"MMS_HOME", ""}, {"LOCALAPPDATA", "/local"}, {"HOME", "/h"}});

    CHECK(app::resolve_workspace_root(std::nullopt, env) == app::default_workspace_root(env));
}

TEST_CASE("An empty option is refused rather than read as the current folder",
          "[application][workspace]") {
    CHECK_THROWS_AS(app::resolve_workspace_root(fs::path(), env_of({})), std::invalid_argument);
}

TEST_CASE("The default root is a folder of the user, named for the application",
          "[application][workspace]") {
    const auto env = env_of({{"LOCALAPPDATA", "/local"},
                             {"USERPROFILE", "/profile"},
                             {"XDG_DATA_HOME", "/xdg"},
                             {"HOME", "/home/me"}});

    const fs::path root = app::default_workspace_root(env);

    const std::string name = root.filename().string();
    CHECK(name == "mms");
    CHECK(root.has_parent_path());
}

TEST_CASE("The default root needs a home to be found in", "[application][workspace]") {
    CHECK_THROWS_AS(app::default_workspace_root(env_of({})), std::runtime_error);
    CHECK_THROWS_AS(app::default_workspace_root(env_of({{"HOME", ""}, {"LOCALAPPDATA", ""}})),
                    std::runtime_error);
}

// ---------------------------------------------------------------- save names

TEST_CASE("Ordinary names are accepted", "[application][workspace]") {
    for (const char* name : {"a", "baseline", "run-1", "run_1", "v1.2.3", "Baseline-2026", "x.y"}) {
        INFO(name);
        CHECK_NOTHROW(app::validate_save_name(name));
    }
}

TEST_CASE("A name of 64 characters is the longest accepted", "[application][workspace]") {
    CHECK_NOTHROW(app::validate_save_name(std::string(64, 'a')));
    CHECK_THROWS_AS(app::validate_save_name(std::string(65, 'a')), std::invalid_argument);
}

TEST_CASE("Names that cannot be a folder anywhere are refused", "[application][workspace]") {
    for (const char* name : {"",
                             "a b",
                             "a/b",
                             "a\\b",
                             "a:b",
                             "a*b",
                             "a?b",
                             "a\"b",
                             "a<b",
                             "a>b",
                             "a|b",
                             "caf\xC3\xA9",
                             ".hidden",
                             "-option",
                             "..",
                             ".",
                             "ends.",
                             "tab\tname"}) {
        INFO(name);
        CHECK_THROWS_AS(app::validate_save_name(name), std::invalid_argument);
    }
}

TEST_CASE("The names Windows keeps for devices are refused, whatever the case or the extension",
          "[application][workspace]") {
    for (const char* name :
         {"CON", "con", "Nul", "AUX", "prn", "COM1", "com9", "LPT3", "nul.txt", "con.log"}) {
        INFO(name);
        CHECK_THROWS_AS(app::validate_save_name(name), std::invalid_argument);
    }
    // And the ones next to them are fine.
    for (const char* name : {"COM0", "COM10", "LPT0", "console", "nulls", "com"}) {
        INFO(name);
        CHECK_NOTHROW(app::validate_save_name(name));
    }
}

// ----------------------------------------------------------- the default name

TEST_CASE("The default name is the scenario and the UTC time", "[application][workspace]") {
    CHECK(app::default_save_name("baseline", at(2026, 10, 26, 14, 5, 9)) ==
          "baseline-20261026-140509");
}

TEST_CASE("Characters a name does not allow become dashes in the default name",
          "[application][workspace]") {
    CHECK(app::default_save_name("my scenario (v2)", at(2026, 1, 2, 3, 4, 5)) ==
          "my-scenario--v2--20260102-030405");
}

TEST_CASE("The default name never starts or ends the scenario part badly",
          "[application][workspace]") {
    CHECK(app::default_save_name(".hidden", at(2026, 1, 2, 3, 4, 5)) == "hidden-20260102-030405");
    CHECK(app::default_save_name("-x", at(2026, 1, 2, 3, 4, 5)) == "x-20260102-030405");
    CHECK(app::default_save_name("name.", at(2026, 1, 2, 3, 4, 5)) == "name-20260102-030405");
}

TEST_CASE("A scenario with no usable name is called run", "[application][workspace]") {
    CHECK(app::default_save_name("", at(2026, 1, 2, 3, 4, 5)) == "run-20260102-030405");
    CHECK(app::default_save_name("...", at(2026, 1, 2, 3, 4, 5)) == "run-20260102-030405");
}

TEST_CASE("A long scenario name is cut so the whole fits in 64 characters",
          "[application][workspace]") {
    const std::string name = app::default_save_name(std::string(200, 'a'), at(2026, 1, 2, 3, 4, 5));

    CHECK(name.size() == 64);
    CHECK(name.ends_with("-20260102-030405"));
    CHECK_NOTHROW(app::validate_save_name(name));
}

TEST_CASE("Every default name is a valid name", "[application][workspace]") {
    for (const char* scenario : {"", "con", "a b", "..", "x/y", "caf\xC3\xA9", "NUL"}) {
        INFO(scenario);
        CHECK_NOTHROW(app::validate_save_name(
            app::default_save_name(scenario, at(2026, 12, 31, 23, 59, 59))));
    }
}

// ---------------------------------------------------------------- the folders

TEST_CASE("Opening a workspace creates the root and its folders", "[application][workspace]") {
    const TempFolder temp;

    const app::Workspace workspace = app::Workspace::open(temp.path / "mms");

    CHECK(workspace.root() == temp.path / "mms");
    CHECK(fs::is_directory(workspace.root()));
    CHECK(fs::is_directory(workspace.scenarios_dir()));
    CHECK(fs::is_directory(workspace.saves_dir()));
    CHECK(fs::is_directory(workspace.logs_dir()));
    CHECK(workspace.settings_file() == temp.path / "mms" / "settings.toml");
    // The settings file is the user's to write: opening does not make one.
    CHECK_FALSE(fs::exists(workspace.settings_file()));
}

TEST_CASE("Opening a workspace again keeps what is in it", "[application][workspace]") {
    const TempFolder temp;
    const app::Workspace first = app::Workspace::open(temp.path);
    std::ofstream(first.scenarios_dir() / "baseline.toml") << "x = 1\n";

    const app::Workspace second = app::Workspace::open(temp.path);

    CHECK(fs::exists(second.scenarios_dir() / "baseline.toml"));
}

TEST_CASE("A root that is a file is refused, and says which", "[application][workspace]") {
    const TempFolder temp;
    fs::create_directories(temp.path);
    const fs::path file = temp.path / "afile";
    std::ofstream(file) << "x";

    CHECK_THROWS_AS(app::Workspace::open(file), std::runtime_error);
    try {
        static_cast<void>(app::Workspace::open(file));
        FAIL("it should have thrown");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("afile") != std::string::npos);
        CHECK(message.find("not a folder") != std::string::npos);
    }
}

TEST_CASE("The folder of a save is under saves, and only for a valid name",
          "[application][workspace]") {
    const TempFolder temp;
    const app::Workspace workspace = app::Workspace::open(temp.path);

    CHECK(workspace.save_dir("baseline") == workspace.saves_dir() / "baseline");
    CHECK_FALSE(fs::exists(workspace.save_dir("baseline"))); // naming a save does not make it
    CHECK_THROWS_AS(workspace.save_dir("../escape"), std::invalid_argument);
    CHECK_THROWS_AS(workspace.save_dir(""), std::invalid_argument);
}

TEST_CASE("Locating a workspace touches nothing on the disk", "[application][workspace]") {
    const TempFolder temp;

    const app::Workspace workspace = app::Workspace::at(temp.path / "nowhere");

    CHECK(workspace.root() == temp.path / "nowhere");
    CHECK(workspace.saves_dir() == temp.path / "nowhere" / "saves");
    CHECK_FALSE(workspace.exists());
    CHECK_FALSE(fs::exists(temp.path)); // not even the folder above it
}

TEST_CASE("A workspace exists once it has been opened, and a located one then agrees",
          "[application][workspace]") {
    const TempFolder temp;
    CHECK_FALSE(app::Workspace::at(temp.path).exists());

    const app::Workspace opened = app::Workspace::open(temp.path);

    CHECK(opened.exists());
    CHECK(app::Workspace::at(temp.path).exists());
}

TEST_CASE("A root that is a file does not count as a workspace", "[application][workspace]") {
    const TempFolder temp;
    fs::create_directories(temp.path);
    const fs::path file = temp.path / "afile";
    std::ofstream(file) << "x";

    CHECK_FALSE(app::Workspace::at(file).exists());
}

TEST_CASE("An empty root is refused by both ways of getting a workspace",
          "[application][workspace]") {
    CHECK_THROWS_AS(app::Workspace::at(fs::path()), std::invalid_argument);
    CHECK_THROWS_AS(app::Workspace::open(fs::path()), std::invalid_argument);
}

// -------------------------------------------------- the default root, per platform

#if defined(_WIN32)
TEST_CASE("On Windows the default root is under LOCALAPPDATA, else under the profile",
          "[application][workspace]") {
    CHECK(app::default_workspace_root(env_of(
              {{"LOCALAPPDATA", "/local"}, {"USERPROFILE", "/u"}})) == fs::path("/local") / "mms");
    CHECK(app::default_workspace_root(env_of({{"USERPROFILE", "/u"}})) ==
          fs::path("/u") / "AppData" / "Local" / "mms");
    // An empty LOCALAPPDATA is as good as none.
    CHECK(app::default_workspace_root(env_of({{"LOCALAPPDATA", ""}, {"USERPROFILE", "/u"}})) ==
          fs::path("/u") / "AppData" / "Local" / "mms");
}
#elif defined(__APPLE__)
TEST_CASE("On macOS the default root is under Application Support", "[application][workspace]") {
    CHECK(app::default_workspace_root(env_of({{"HOME", "/Users/me"}})) ==
          fs::path("/Users/me") / "Library" / "Application Support" / "mms");
}
#else
TEST_CASE("Elsewhere the default root is under XDG_DATA_HOME, else under the home",
          "[application][workspace]") {
    CHECK(app::default_workspace_root(env_of({{"XDG_DATA_HOME", "/xdg"}, {"HOME", "/home/me"}})) ==
          fs::path("/xdg") / "mms");
    CHECK(app::default_workspace_root(env_of({{"HOME", "/home/me"}})) ==
          fs::path("/home/me") / ".local" / "share" / "mms");
    CHECK(app::default_workspace_root(env_of({{"XDG_DATA_HOME", ""}, {"HOME", "/home/me"}})) ==
          fs::path("/home/me") / ".local" / "share" / "mms");
}
#endif
