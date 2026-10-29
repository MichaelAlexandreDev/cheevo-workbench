#include "cw/devtools/user_file_store.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace {

using cw_test::TempDir;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open test file for reading: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size()))) {
        throw std::runtime_error("Could not write test file: " + path.string());
    }
}

cw::UserFileDocument make_document(std::string title = "Synthetic local achievement") {
    std::string error;
    const auto logic = cw::parse_achievement_logic("0xH000001=1", error);
    if (!logic) {
        throw std::runtime_error("Could not build synthetic test logic: " + error);
    }

    cw::UserFileDocument document;
    document.title = std::move(title);

    cw::LocalAchievement achievement;
    achievement.id = 0;
    achievement.logic = *logic;
    achievement.title = "Synthetic achievement";
    achievement.description = "Uses synthetic memory only";

    cw::UserFileEntry entry;
    entry.asset = std::move(achievement);
    document.entries.push_back(std::move(entry));
    return document;
}

std::string serialize(const cw::UserFileDocument& document) {
    std::string error;
    const auto serialized = cw::serialize_user_file(document, error);
    if (!serialized) {
        throw std::runtime_error("Could not serialize test document: " + error);
    }
    return *serialized;
}

void check_no_temporary_files(const std::filesystem::path& directory,
                              const std::filesystem::path& destination) {
    const std::string prefix = "." + destination.filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        CHECK(entry.path().filename().string().rfind(prefix, 0) != 0);
    }
}

class ScopedCurrentPath {
  public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }
    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }
    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

  private:
    std::filesystem::path previous_;
};

} // namespace

TEST_CASE("A missing user file loads without creating paths and saves atomically") {
    TempDir dir;
    const auto parent = dir.path() / "new" / "nested";
    const auto file = parent / "18238-User.txt";
    cw::UserFileStore store;
    std::string error;

    REQUIRE(store.load(file, error));
    CHECK(error.empty());
    CHECK(store.path() == file);
    CHECK(store.writable());
    CHECK(store.document().entries.empty());
    CHECK(store.document().title.empty());
    CHECK_FALSE(std::filesystem::exists(parent));

    const auto document = make_document();
    const auto expected = serialize(document);
    REQUIRE(store.save(document, error));
    CHECK(error.empty());
    CHECK(read_bytes(file) == expected);
    CHECK(store.writable());
    CHECK(store.document().title == document.title);
    check_no_temporary_files(parent, file);

    // The store advances its disk snapshot only after a successful replacement.
    REQUIRE(store.save(document, error));
    CHECK(read_bytes(file) == expected);
    check_no_temporary_files(parent, file);

    cw::UserFileStore reloaded;
    REQUIRE(reloaded.load(file, error));
    const auto normalized = serialize(reloaded.document());
    CHECK(normalized == expected);
}

TEST_CASE("A simple relative filename works when its parent path is empty") {
    TempDir dir;
    ScopedCurrentPath current_path(dir.path());
    const std::filesystem::path file("relative-user.txt");
    cw::UserFileStore store;
    std::string error;

    REQUIRE(store.load(file, error));
    CHECK(store.writable());
    CHECK_FALSE(std::filesystem::exists(file));
    REQUIRE(store.save(make_document(), error));
    CHECK(std::filesystem::exists(file));
    CHECK(read_bytes(file) == serialize(store.document()));
    check_no_temporary_files(".", file);
}

TEST_CASE("Saving a loaded relative path after changing directories still targets its original") {
    TempDir dir;
    const auto first_directory = dir.path() / "first";
    const auto second_directory = dir.path() / "second";
    std::filesystem::create_directories(first_directory);
    std::filesystem::create_directories(second_directory);
    const auto first_file = first_directory / "relative-user.txt";
    const auto original = serialize(make_document("Loaded in the first directory"));
    write_bytes(first_file, original);

    ScopedCurrentPath current_path(first_directory);
    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load("relative-user.txt", error));
    CHECK(store.path().is_absolute());

    std::filesystem::current_path(second_directory);
    REQUIRE(store.save(store.document(), error));
    CHECK(read_bytes(first_file) == original);
    CHECK_FALSE(std::filesystem::exists(second_directory / "relative-user.txt"));
    check_no_temporary_files(first_directory, first_file);
    check_no_temporary_files(second_directory, first_file);
}

TEST_CASE("Saving the loaded document persists a normalized short badge") {
    TempDir dir;
    const auto file = dir.path() / "18238-User.txt";
    const std::string original =
        "writer\n"
        "Game\n"
        "17:\"0xH0001=1\":Title:Description:::progression:Author:10:::::123\n";
    write_bytes(file, original);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(file, error));
    REQUIRE(store.document().entries.size() == 1);
    const auto* achievement =
        std::get_if<cw::LocalAchievement>(&store.document().entries.front().asset);
    REQUIRE(achievement != nullptr);
    CHECK(achievement->badge == "00123");

    const auto unchanged = serialize(store.document());
    CHECK(unchanged == original);
    REQUIRE(store.save(store.document(), error));
    CHECK(read_bytes(file) == original);

    auto edited = store.document();
    auto* edited_achievement = std::get_if<cw::LocalAchievement>(&edited.entries.front().asset);
    REQUIRE(edited_achievement != nullptr);
    edited_achievement->badge = "42";
    const auto edited_bytes = serialize(edited);
    CHECK(edited_bytes.find(":00042\n") != std::string::npos);

    REQUIRE(store.save(edited, error));
    const auto* saved_achievement =
        std::get_if<cw::LocalAchievement>(&store.document().entries.front().asset);
    REQUIRE(saved_achievement != nullptr);
    CHECK(saved_achievement->badge == "00042");
    CHECK(read_bytes(file) == edited_bytes);

    REQUIRE(store.save(store.document(), error));
    CHECK(read_bytes(file) == edited_bytes);
}

TEST_CASE("A failed load resets the document, records the path, and blocks saves") {
    TempDir dir;
    const auto valid_file = dir.path() / "valid-user.txt";
    const auto file = dir.path() / "18238-User.txt";
    const std::string malformed = "header\ntitle\n1:\n";
    write_bytes(valid_file, serialize(make_document("Previously loaded")));
    write_bytes(file, malformed);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(valid_file, error));
    CHECK(store.document().title == "Previously loaded");
    REQUIRE_FALSE(store.load(file, error));
    CHECK_FALSE(error.empty());
    CHECK(store.path() == file);
    CHECK_FALSE(store.writable());
    CHECK(store.document().title.empty());
    CHECK(store.document().entries.empty());

    error.clear();
    CHECK_FALSE(store.save(make_document(), error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == malformed);
}

TEST_CASE("Invalid in-memory documents never replace the last valid file") {
    TempDir dir;
    const auto file = dir.path() / "18238-User.txt";
    const auto original = serialize(make_document());
    write_bytes(file, original);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(file, error));
    auto invalid = make_document();
    auto& achievement = std::get<cw::LocalAchievement>(invalid.entries.front().asset);
    achievement.logic.core.conditions.front().flag = static_cast<cw::LogicFlag>(255);

    CHECK_FALSE(store.save(invalid, error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == original);
    check_no_temporary_files(dir.path(), file);
}

TEST_CASE("Saving keeps the existing file's permission bits") {
    TempDir dir;
    const auto file = dir.path() / "18238-User.txt";
    write_bytes(file, serialize(make_document()));
    REQUIRE(::chmod(file.c_str(), 0640) == 0);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(file, error));
    REQUIRE(store.save(make_document("Edited synthetic document"), error));

    struct stat file_status{};
    REQUIRE(::stat(file.c_str(), &file_status) == 0);
    CHECK((file_status.st_mode & 0777) == 0640);
}

TEST_CASE("External edits, including malformed replacements, block a save") {
    TempDir dir;
    const auto file = dir.path() / "18238-User.txt";
    const auto original = serialize(make_document());
    write_bytes(file, original);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(file, error));
    const std::string external = "header\ntitle\n1:\n";
    write_bytes(file, external);

    CHECK_FALSE(store.save(make_document("A newer local title"), error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(file) == external);
    check_no_temporary_files(dir.path(), file);
}

TEST_CASE("Only bounded regular files are loaded and symbolic links are refused") {
    TempDir dir;
    cw::UserFileStore store;
    std::string error;

    const auto directory = dir.path() / "directory";
    std::filesystem::create_directory(directory);
    CHECK_FALSE(store.load(directory, error));
    CHECK_FALSE(store.writable());

    const auto fifo = dir.path() / "pipe";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    error.clear();
    CHECK_FALSE(store.load(fifo, error));
    CHECK_FALSE(store.writable());

    const auto target = dir.path() / "target.txt";
    write_bytes(target, serialize(make_document()));
    const auto link = dir.path() / "linked-user.txt";
    std::filesystem::create_symlink(target, link);
    error.clear();
    CHECK_FALSE(store.load(link, error));
    CHECK_FALSE(store.writable());
    CHECK(read_bytes(target) == serialize(make_document()));

    const auto oversized = dir.path() / "oversized.txt";
    write_bytes(oversized, std::string(cw::kMaxUserFileSize + 1, 'x'));
    error.clear();
    CHECK_FALSE(store.load(oversized, error));
    CHECK_FALSE(store.writable());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Save failures for a non-directory parent or directory target leave no temporaries") {
    TempDir dir;
    std::string error;

    const auto parent_file = dir.path() / "parent-file";
    const auto nested_target = parent_file / "18238-User.txt";
    cw::UserFileStore blocked_parent;
    REQUIRE(blocked_parent.load(nested_target, error));
    CHECK(blocked_parent.writable());
    write_bytes(parent_file, "not a directory");
    error.clear();
    CHECK_FALSE(blocked_parent.save(make_document(), error));
    CHECK_FALSE(error.empty());
    CHECK(read_bytes(parent_file) == "not a directory");
    check_no_temporary_files(dir.path(), nested_target);

    const auto directory_target = dir.path() / "directory-target";
    cw::UserFileStore blocked_target;
    REQUIRE(blocked_target.load(directory_target, error));
    std::filesystem::create_directory(directory_target);
    error.clear();
    CHECK_FALSE(blocked_target.save(make_document(), error));
    CHECK_FALSE(error.empty());
    CHECK(std::filesystem::is_directory(directory_target));
    CHECK(std::filesystem::is_empty(directory_target));
    check_no_temporary_files(dir.path(), directory_target);
}

TEST_CASE("A file size limit failure leaves the loaded file intact and removes its temporary") {
    TempDir dir;
    const auto file = dir.path() / "18238-User.txt";
    const auto original = serialize(make_document("Original document"));
    write_bytes(file, original);

    cw::UserFileStore store;
    std::string error;
    REQUIRE(store.load(file, error));
    const auto oversized_document = make_document(std::string(4096, 'T'));

    struct rlimit limits{};
    REQUIRE_MESSAGE(::getrlimit(RLIMIT_FSIZE, &limits) == 0,
                    "getrlimit(RLIMIT_FSIZE) is unavailable in this environment");
    REQUIRE_MESSAGE(limits.rlim_max >= static_cast<rlim_t>(64),
                    "The hard RLIMIT_FSIZE limit is below the required 64-byte child limit");

    const pid_t child = ::fork();
    REQUIRE_MESSAGE(child >= 0, "fork() is unavailable in this environment");
    if (child == 0) {
        struct sigaction action{};
        action.sa_handler = SIG_IGN;
        ::sigemptyset(&action.sa_mask);
        if (::sigaction(SIGXFSZ, &action, nullptr) != 0) {
            ::_exit(10);
        }

        struct rlimit child_limits{};
        if (::getrlimit(RLIMIT_FSIZE, &child_limits) != 0 ||
            child_limits.rlim_max < static_cast<rlim_t>(64)) {
            ::_exit(11);
        }
        child_limits.rlim_cur = static_cast<rlim_t>(64);
        if (::setrlimit(RLIMIT_FSIZE, &child_limits) != 0) {
            ::_exit(12);
        }

        std::string child_error;
        if (store.save(oversized_document, child_error)) {
            ::_exit(20);
        }
        if (child_error.empty() || child_error.find("writ") == std::string::npos) {
            ::_exit(21);
        }
        ::_exit(0);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE_MESSAGE(waited == child, "waitpid() failed while collecting the child save result");
    REQUIRE_MESSAGE(WIFEXITED(status), "The child did not exit normally");
    const int child_status = WEXITSTATUS(status);
    CHECK_MESSAGE(child_status != 10, "The child could not ignore SIGXFSZ");
    CHECK_MESSAGE(child_status != 11, "The child could not read RLIMIT_FSIZE");
    CHECK_MESSAGE(child_status != 12, "The child could not set RLIMIT_FSIZE");
    CHECK_MESSAGE(child_status == 0,
                  "The child save did not fail with a write diagnostic as expected");
    CHECK(read_bytes(file) == original);
    check_no_temporary_files(dir.path(), file);
}
