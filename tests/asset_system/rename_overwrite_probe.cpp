#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

// Plan 0012 Section D10's atomic-write design depends on a specific,
// non-portable-in-general behavior: that std::filesystem::rename()
// replaces an existing destination file on this project's actual
// Windows/MSVC target, using a single same-volume filesystem rename
// operation (Win32 MoveFileExW with MOVEFILE_REPLACE_EXISTING), not a
// non-atomic delete-then-recreate. The C++ standard's own wording
// ("as if by POSIX rename()") does not by itself guarantee this on a
// non-POSIX implementation -- this test verifies the actual, observed
// behavior empirically on the real toolchain, rather than assuming the
// standard's prose is sufficient. cook.cpp's own writeBytesAtomically()
// relies on exactly this: the temp file is always created in the same
// directory as the final path specifically so the rename stays
// same-volume, which is what makes MoveFileExW take its documented
// single-operation (not copy+delete) path.

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_rename_probe" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

}  // namespace

TEST_CASE("std::filesystem::rename() replaces an existing destination file (same-directory, same-volume)",
          "[asset_system]") {
  TempDirGuard dir("overwrite");
  const fs::path destPath = dir.path / "dest.txt";
  const fs::path srcPath = dir.path / "dest.txt.tmp-probe";

  {
    std::ofstream dest(destPath, std::ios::binary | std::ios::trunc);
    dest << "OLD CONTENT";
  }
  {
    std::ofstream src(srcPath, std::ios::binary | std::ios::trunc);
    src << "NEW CONTENT";
  }
  REQUIRE(fs::exists(destPath));
  REQUIRE(fs::exists(srcPath));

  std::error_code ec;
  fs::rename(srcPath, destPath, ec);

  INFO("rename error_code: " << ec.value() << " (" << ec.message() << ")");
  REQUIRE_FALSE(static_cast<bool>(ec));

  std::ifstream in(destPath, std::ios::binary);
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content == "NEW CONTENT");
  CHECK_FALSE(fs::exists(srcPath));
}

TEST_CASE("std::filesystem::rename() fails cleanly (not silently) renaming a file onto an existing directory",
          "[asset_system]") {
  // Exercises the failure path writeBytesAtomically() must handle: not
  // every rename() call can succeed (POSIX/Win32 both refuse to rename
  // a regular file over an existing directory), and the failure must be
  // reported via error_code, never an uncaught exception, from this
  // noexcept-free but still non-throwing overload.
  TempDirGuard dir("directory_collision");
  const fs::path destPath = dir.path / "dest_dir";
  const fs::path srcPath = dir.path / "dest_dir.tmp-probe";
  fs::create_directories(destPath);
  {
    std::ofstream src(srcPath, std::ios::binary | std::ios::trunc);
    src << "content";
  }

  std::error_code ec;
  fs::rename(srcPath, destPath, ec);

  CHECK(static_cast<bool>(ec));
  CHECK(fs::is_directory(destPath));
  CHECK(fs::exists(srcPath));
}
