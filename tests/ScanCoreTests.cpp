// ScanCoreTests.cpp : tests for ScanCore. These run without Alibre.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "FileIO.h"

namespace fs = std::filesystem;
using namespace scancore;

TEST_CASE("files are written atomically and read back")
{
    static std::atomic<int> counter{ 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path folder = fs::temp_directory_path() /
                            ("ScanCoreTests-" + std::to_string(stamp) + "-" + std::to_string(counter++));
    fs::create_directories(folder);

    const fs::path path = folder / "file.txt";
    std::string message;
    CHECK(WriteFileAtomically(path, "first", message));
    CHECK(WriteFileAtomically(path, "second", message));

    std::string text;
    CHECK(ReadWholeFile(path, text));
    CHECK(text == "second");
    CHECK_FALSE(fs::exists(folder / "file.txt.tmp"));

    std::error_code ignored;
    fs::remove_all(folder, ignored);
}
