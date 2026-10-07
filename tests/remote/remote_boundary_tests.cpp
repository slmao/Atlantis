#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Plan 0055 M1 (ADR-0106 D7; Spec 0055 ruling Q6, M-a): Atlantis Remote's
// boundary, held like the Vulkan Backend's private WSI code --
//   - its public headers name no OS header or OS type;
//   - OS headers appear only in its private src/os/ translation units;
//   - it includes nothing of Runtime, the ECS, Platform, RHI, Renderer or
//     Vulkan: only Connection, World's access value types and Core/Asset
//     System value headers;
//   - its targets link Atlantis::Connection (and, on Windows, ws2_32) only.

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Include {
  fs::path file;
  std::string target;
  bool quoted = false;
};

[[nodiscard]] std::vector<fs::path> sourcesUnder(const fs::path& root) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() && (extension == ".h" || extension == ".cpp")) files.push_back(entry.path());
  }
  return files;
}

[[nodiscard]] std::vector<Include> includesOf(const fs::path& file) {
  std::vector<Include> found;
  const std::string source = readFile(file);
  std::size_t at = 0;
  while ((at = source.find("#include", at)) != std::string::npos) {
    const auto open = source.find_first_of("<\"", at);
    const auto close = source.find_first_of(">\"", open + 1);
    found.push_back({file, source.substr(open + 1, close - open - 1), source[open] == '"'});
    at = close;
  }
  return found;
}

const std::vector<std::string_view> kOsHeaders = {"winsock2.h", "ws2tcpip.h", "windows.h", "sys/", "netinet/",
                                                  "arpa/",      "unistd.h",   "fcntl.h",   "poll.h"};
[[nodiscard]] bool isOsHeader(std::string_view target) {
  return std::any_of(kOsHeaders.begin(), kOsHeaders.end(),
                     [&](std::string_view os) { return target == os || target.starts_with(os); });
}

const std::vector<std::string_view> kAllowedAtlantis = {
    "atlantis/remote/",   "atlantis/connection/", "atlantis/world/access/",           "atlantis/schema.h",
    "atlantis/result.h",  "atlantis/assert.h",    "atlantis/asset_system/asset_guid.h",
};

// What a Remote source may include: the allowlist, standard headers, its
// own private headers (quoted, existing under src/remote/src), and -- in
// src/os/ only -- OS headers.
[[nodiscard]] std::vector<std::string> violations(const std::vector<Include>& includes, const fs::path& privateRoot) {
  std::vector<std::string> out;
  for (const Include& include : includes) {
    const std::string_view target = include.target;
    const bool inOsLayer = include.file.parent_path().filename() == "os";
    bool allowed = false;
    if (target.starts_with("atlantis/")) {
      allowed = std::any_of(kAllowedAtlantis.begin(), kAllowedAtlantis.end(),
                            [&](std::string_view prefix) { return target.starts_with(prefix); });
    } else if (include.quoted) {
      allowed = fs::exists(include.file.parent_path() / include.target) || fs::exists(privateRoot / include.target);
    } else if (isOsHeader(target)) {
      allowed = inOsLayer;
    } else {
      allowed = target.find('/') == std::string_view::npos && !target.ends_with(".h");
    }
    if (!allowed) out.push_back(include.file.string() + ": " + include.target);
  }
  return out;
}

}  // namespace

TEST_CASE("Atlantis Remote: public headers name no OS header or OS type", "[remote][module_boundary]") {
  const fs::path publicRoot = fs::path(ATLANTIS_REMOTE_SOURCE_DIR) / "include";
  const std::vector<fs::path> headers = sourcesUnder(publicRoot);
  REQUIRE(headers.size() >= 3);
  for (const fs::path& header : headers) {
    INFO(header.string());
    for (const Include& include : includesOf(header)) {
      INFO(include.target);
      CHECK_FALSE(isOsHeader(include.target));
    }
    const std::string text = readFile(header);
    for (const std::string_view token : {"SOCKET", "sockaddr", "HANDLE", "fd_set", "WSA", "DWORD", "socklen_t",
                                         "pid_t", "HWND", "<windows.h>", "<sys/"}) {
      INFO(token);
      CHECK(text.find(token) == std::string::npos);
    }
  }
}

TEST_CASE("Atlantis Remote: OS headers only in src/os/, and nothing of Runtime, the ECS or the GPU stack",
          "[remote][module_boundary]") {
  const fs::path root(ATLANTIS_REMOTE_SOURCE_DIR);
  std::vector<Include> includes;
  std::size_t osIncludes = 0;
  for (const fs::path& file : sourcesUnder(root)) {
    for (Include& include : includesOf(file)) {
      if (isOsHeader(include.target)) ++osIncludes;
      includes.push_back(std::move(include));
    }
  }
  REQUIRE(includes.size() >= 20);  // the scan is not vacuous
  CHECK(osIncludes >= 2);          // and it does see the OS layer
  const std::vector<std::string> bad = violations(includes, root / "src");
  std::string text;
  for (const auto& v : bad) text += v + '\n';
  INFO(text);
  CHECK(bad.empty());

  // The rule bites.
  const fs::path outside = root / "src" / "remote_server.cpp";
  CHECK(violations({{outside, "winsock2.h", false}, {outside, "sys/socket.h", false},
                    {outside, "atlantis/runtime/runtime_application.h", false},
                    {outside, "atlantis/world/ecs/world.h", false}, {outside, "atlantis/platform/platform.h", false},
                    {outside, "atlantis/rhi/device.h", false}, {outside, "vulkan/vulkan.h", false},
                    {outside, "no_such_private_header.h", true}},
                   root / "src")
            .size() == 8);
  CHECK(violations({{root / "src" / "os" / "socket_win32.cpp", "winsock2.h", false}}, root / "src").empty());
}

TEST_CASE("Atlantis Remote links Atlantis::Connection (and ws2_32) only", "[remote][module_boundary]") {
  const std::string cmake = readFile(fs::path(ATLANTIS_REMOTE_SOURCE_DIR) / "CMakeLists.txt");
  std::size_t blocks = 0;
  std::size_t at = 0;
  while ((at = cmake.find("target_link_libraries(", at)) != std::string::npos) {
    const auto end = cmake.find(')', at);
    std::string block = cmake.substr(at + 22, end - at - 22);
    at = end;
    ++blocks;
    for (const std::string_view word :
         {"atlantis_remote_wire", "atlantis_remote_client", "atlantis_remote_server", "PUBLIC", "PRIVATE",
          "Atlantis::Connection", "atlantis_compiler_warnings", "ws2_32"}) {
      std::size_t found = 0;
      while ((found = block.find(word)) != std::string::npos) block.erase(found, word.size());
    }
    INFO(block);
    CHECK(block.find_first_not_of(" \t\r\n") == std::string::npos);
  }
  CHECK(blocks == 4);
}
