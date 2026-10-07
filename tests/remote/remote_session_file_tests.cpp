#include <atlantis/remote/remote_client.h>
#include <atlantis/remote/remote_server.h>
#include <atlantis/remote/session_file.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

// Plan 0055 P4: the session file -- its format, the token, a write through a
// renamed temporary, removal on shutdown, and the client's resolution order
// (--session, then ATLANTIS_SESSION, then ./.atlantis/runtime.session.json).

namespace {

namespace fs = std::filesystem;
using atlantis::remote::SessionFileError;
using atlantis::remote::SessionInfo;

[[nodiscard]] SessionInfo sample() {
  SessionInfo session;
  session.port = 54321;
  session.token = "0123456789abcdef0123456789abcdef";
  session.pid = 4242;
  session.scene = atlantis::asset_system::parseAssetGuid("00550055-0055-4055-8055-005500550055").value();
  return session;
}

[[nodiscard]] fs::path scratch() {
  const fs::path dir = fs::temp_directory_path() / "atlantis_remote_session_tests" /
                       std::to_string(std::random_device{}());
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST_CASE("session file: the format is one JSON object that parses back", "[remote][session]") {
  const SessionInfo session = sample();
  const std::string text = atlantis::remote::formatSession(session);
  CHECK(text ==
        "{\"protocol\":\"atlantis.remote/1\",\"port\":54321,\"token\":\"0123456789abcdef0123456789abcdef\","
        "\"pid\":4242,\"scene\":\"00550055-0055-4055-8055-005500550055\"}\n");
  const auto parsed = atlantis::remote::parseSession(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == session);

  CHECK(atlantis::remote::parseSession("{}").error() == SessionFileError::Malformed);
  CHECK(atlantis::remote::parseSession("not json").error() == SessionFileError::Malformed);
  CHECK(atlantis::remote::parseSession(R"({"protocol":"atlantis.remote/2","port":1,"token":"t","pid":1,)"
                                       R"("scene":"00550055-0055-4055-8055-005500550055"})")
            .error() == SessionFileError::WrongProtocol);
  CHECK(atlantis::remote::parseSession(R"({"protocol":"atlantis.remote/1","port":70000,"token":"t","pid":1,)"
                                       R"("scene":"00550055-0055-4055-8055-005500550055"})")
            .error() == SessionFileError::Malformed);
}

TEST_CASE("session file: tokens are 32 hex digits and differ per run", "[remote][session]") {
  const std::string a = atlantis::remote::generateToken();
  const std::string b = atlantis::remote::generateToken();
  CHECK(a.size() == 32);
  CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
  CHECK(a != b);
}

TEST_CASE("session file: written through a temporary, read back, removed", "[remote][session]") {
  const fs::path dir = scratch();
  const fs::path path = dir / ".atlantis" / "runtime.session.json";  // the directory is created
  REQUIRE(atlantis::remote::writeSessionFile(path, sample()).isOk());
  CHECK_FALSE(fs::exists(fs::path(path).concat(".tmp")));
  const auto read = atlantis::remote::readSessionFile(path);
  REQUIRE(read.isOk());
  CHECK(read.value() == sample());

  SessionInfo next = sample();
  next.port = 1234;
  REQUIRE(atlantis::remote::writeSessionFile(path, next).isOk());  // replaces
  CHECK(atlantis::remote::readSessionFile(path).value().port == 1234);

  atlantis::remote::removeSessionFile(path);
  CHECK(atlantis::remote::readSessionFile(path).error() == SessionFileError::NotFound);
  atlantis::remote::removeSessionFile(path);  // absent: no effect
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("session file: the client resolves --session, then ATLANTIS_SESSION, then the default",
          "[remote][session]") {
  using atlantis::remote::resolveSessionPath;
  CHECK(resolveSessionPath(std::string("given.json"), "env.json") == fs::path("given.json"));
  CHECK(resolveSessionPath(std::nullopt, "env.json") == fs::path("env.json"));
  CHECK(resolveSessionPath(std::nullopt, "") == atlantis::remote::defaultSessionPath());
  CHECK(resolveSessionPath(std::nullopt, nullptr) == atlantis::remote::defaultSessionPath());
  CHECK(atlantis::remote::defaultSessionPath() == fs::path(".atlantis") / "runtime.session.json");
}
