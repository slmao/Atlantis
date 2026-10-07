// Plan 0055 M7 (Spec 0055 R1, R9; rulings Q7 A-b + L-a; J9): the completion
// loop across two processes -- `atlantis_runtime --listen 0` and the
// `atlantis` executable -- one assertion per step of the completion
// definition: discover the schema, query the world, find the entity, modify
// a component, step the Runtime, capture the frame, read diagnostics, verify
// the result. Then a REPL smoke, and a graceful close: WM_CLOSE to the
// Runtime's window and a 0 exit code (a forced kill fails the test).
//
// The process helper below is test-only Windows code (J9): CreateProcessW
// with the child's stdin/stdout/stderr on pipes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace json = atlantis::connection::json;

[[nodiscard]] std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// The standard Windows argument quoting (CommandLineToArgvW's inverse).
[[nodiscard]] std::wstring quote(const std::wstring& argument) {
  if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
    else out.append(backslashes, L'\\');
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, L'\\');
  out += L'"';
  return out;
}

// A child process with its three standard streams on pipes; output is read
// by two threads so neither pipe can fill and stall the child.
class Child {
 public:
  Child(const fs::path& exe, const std::vector<std::string>& arguments) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errWrite = nullptr;
    REQUIRE(CreatePipe(&inRead, &stdinWrite_, &inherit, 0));
    REQUIRE(CreatePipe(&stdoutRead_, &outWrite, &inherit, 0));
    REQUIRE(CreatePipe(&stderrRead_, &errWrite, &inherit, 0));
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead_, HANDLE_FLAG_INHERIT, 0);
    std::wstring commandLine = quote(exe.wstring());
    for (const std::string& argument : arguments) commandLine += L" " + quote(widen(argument));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inRead;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(exe.wstring().c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    REQUIRE(created);
    process_ = info.hProcess;
    processId_ = info.dwProcessId;
    CloseHandle(info.hThread);
    outReader_ = std::thread([this] { drain(stdoutRead_, out_); });
    errReader_ = std::thread([this] { drain(stderrRead_, err_); });
  }
  ~Child() {
    closeStdin();
    if (process_ != nullptr && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) TerminateProcess(process_, 99);
    if (outReader_.joinable()) outReader_.join();
    if (errReader_.joinable()) errReader_.join();
    if (process_ != nullptr) CloseHandle(process_);
    CloseHandle(stdoutRead_);
    CloseHandle(stderrRead_);
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  void write(std::string_view text) {
    DWORD written = 0;
    REQUIRE(WriteFile(stdinWrite_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
  }
  void closeStdin() {
    if (stdinWrite_ != nullptr) CloseHandle(stdinWrite_);
    stdinWrite_ = nullptr;
  }
  // The exit code, once it exits within `timeout`.
  [[nodiscard]] std::optional<DWORD> wait(std::chrono::milliseconds timeout) {
    if (WaitForSingleObject(process_, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    if (outReader_.joinable()) outReader_.join();
    if (errReader_.joinable()) errReader_.join();
    return code;
  }
  [[nodiscard]] DWORD processId() const noexcept { return processId_; }
  [[nodiscard]] const std::string& out() const noexcept { return out_; }
  [[nodiscard]] const std::string& err() const noexcept { return err_; }

 private:
  static void drain(HANDLE pipe, std::string& sink) {
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) sink.append(buffer, read);
  }

  HANDLE stdinWrite_ = nullptr;
  HANDLE stdoutRead_ = nullptr;
  HANDLE stderrRead_ = nullptr;
  HANDLE process_ = nullptr;
  DWORD processId_ = 0;
  std::string out_;
  std::string err_;
  std::thread outReader_;
  std::thread errReader_;
};

// The top-level windows of a process.
[[nodiscard]] std::vector<HWND> windowsOf(DWORD processId) {
  struct Search {
    DWORD processId;
    std::vector<HWND> found;
  } search{processId, {}};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto* s = reinterpret_cast<Search*>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner == s->processId) s->found.push_back(window);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}

struct Run {
  int exitCode = -1;
  std::string out;
  std::string err;
};

// `atlantis <arguments>` to completion.
[[nodiscard]] Run runAtlantis(const fs::path& session, std::vector<std::string> arguments, std::string_view input = {}) {
  arguments.insert(arguments.begin(), {"--session", session.string()});
  Child child(ATLANTIS_CLI_EXE, arguments);
  if (!input.empty()) child.write(input);
  child.closeStdin();
  const auto code = child.wait(std::chrono::seconds(120));
  REQUIRE(code.has_value());
  return Run{static_cast<int>(*code), child.out(), child.err()};
}

// The one envelope a --json invocation prints, which must say ok.
[[nodiscard]] json::Value okResult(const Run& run) {
  INFO("stdout: " << run.out << "\nstderr: " << run.err);
  REQUIRE(run.exitCode == 0);
  auto envelope = json::parse(run.out);
  REQUIRE(envelope.isOk());
  REQUIRE(envelope.value().find("atlantis")->asString() == "cli/1");
  REQUIRE(envelope.value().find("ok")->asBool());
  REQUIRE(envelope.value().find("diagnostics")->isArray());
  return *envelope.value().find("result");
}

[[nodiscard]] float asFloat(const json::Value& value) {
  float out = 0.0f;
  REQUIRE(value.toFloat(out));
  return out;
}

}  // namespace

TEST_CASE("atlantis drives atlantis_runtime --listen through the completion loop, and both exit cleanly",
          "[cli][e2e][gpu]") {
  const fs::path dir = fs::temp_directory_path() / "atlantis_cli_e2e" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  const fs::path session = dir / "runtime.session.json";
  Child runtime(ATLANTIS_RUNTIME_EXE,
                {"--scene", "integrated_showcase_demo", "--listen", "0", "--session-file", session.string()});
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!fs::exists(session) && std::chrono::steady_clock::now() < deadline) {
    REQUIRE_FALSE(runtime.wait(std::chrono::milliseconds(100)).has_value());  // still running
  }
  REQUIRE(fs::exists(session));
  const std::string light = "0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea";

  // 1. Discover the schema.
  {
    const json::Value types = okResult(runAtlantis(session, {"schema", "list", "--json"}));
    bool hasLight = false;
    for (const json::Value& type : types.asArray()) hasLight = hasLight || type.find("qualified")->asString() == "world::Light";
    CHECK(hasLight);
  }
  // 2. Query the world.
  {
    const json::Value world = okResult(runAtlantis(session, {"world", "inspect", "--json"}));
    std::uint64_t lights = 0;
    REQUIRE(world.find("components")->find("Light") != nullptr);
    CHECK((world.find("components")->find("Light")->toUInt64(lights) && lights == 1));
  }
  // 3. Find the entity.
  {
    const json::Value found = okResult(
        runAtlantis(session, {"entity", "list", "--with", "Light", "--where", "Light.kind=Directional", "--json"}));
    REQUIRE(found.asArray().size() == 1);
    CHECK(found.asArray()[0].find("guid")->asString() == light);
  }
  // 4. Modify a component (running: the outcome arrives with the next frame).
  {
    const json::Value set = okResult(runAtlantis(session, {"property", "set", light, "Light.intensity", "6", "--json"}));
    const json::Value& event = set.find("outcome")->find("events")->asArray().at(0);
    CHECK((event.find("kind")->asString() == "PropertyChanged" && asFloat(*event.find("value")) == 6.0f));
  }
  // 5. Step the Runtime (paused, so the step is the frame that shows it).
  CHECK(okResult(runAtlantis(session, {"runtime", "pause", "--json"})).find("paused")->asBool());
  // 6. Capture the frame.
  const fs::path shot = dir / "shot.png";
  const Run stepped = runAtlantis(session, {"runtime", "step", "--capture", shot.string(), "--json"});
  const json::Value step = okResult(stepped);
  CHECK((fs::exists(shot) && step.find("capture")->find("image")->find("path")->asString() == shot.string()));
  // 7. Read diagnostics: every envelope carries the Runtime's; an error
  //    also goes to stderr as JSON under --diagnostics=json.
  {
    const Run refused =
        runAtlantis(session, {"property", "get", light, "Camera.nearZ", "--json", "--diagnostics=json"});
    const auto line = json::parse(refused.err.substr(0, refused.err.find('\n')));
    CHECK((refused.exitCode == 1 && line.isOk() && line.value().find("code")->asString() == "ComponentMissing" &&
           line.value().find("category")->asString() == "refused"));
  }
  // 8. Verify: the captured frame's data carries the new intensity.
  {
    const json::Value& frameData = *step.find("capture")->find("frameData");
    REQUIRE(frameData.find("directionalLights")->asArray().size() == 1);
    CHECK(asFloat(*frameData.find("directionalLights")->asArray()[0].find("intensity")) == 6.0f);
  }

  // The REPL: three commands, three envelopes; the prompt is on stderr only.
  {
    const Run repl = runAtlantis(session, {"repl", "--json"}, "world list\nruntime resume\nschema inspect Light\n");
    REQUIRE(repl.exitCode == 0);
    std::size_t envelopes = 0;
    std::size_t start = 0;
    while (start < repl.out.size()) {
      const std::size_t end = repl.out.find('\n', start);
      const std::string_view line = std::string_view(repl.out).substr(start, end - start);
      if (!line.empty() && line != "\r") {
        const auto envelope = json::parse(line);
        CHECK((envelope.isOk() && envelope.value().find("ok")->asBool()));
        ++envelopes;
      }
      if (end == std::string::npos) break;
      start = end + 1;
    }
    CHECK(envelopes == 3);
    CHECK(repl.out.find("atlantis>") == std::string::npos);
    CHECK(repl.err.find("atlantis> ") != std::string::npos);
  }

  // Graceful close: WM_CLOSE to the Runtime's window, then a 0 exit code.
  const std::vector<HWND> windows = windowsOf(runtime.processId());
  REQUIRE_FALSE(windows.empty());
  for (const HWND window : windows) PostMessageW(window, WM_CLOSE, 0, 0);
  const auto exited = runtime.wait(std::chrono::seconds(30));
  INFO("runtime stderr tail: " << runtime.err().substr(runtime.err().size() > 2000 ? runtime.err().size() - 2000 : 0));
  REQUIRE(exited.has_value());  // a forced kill (the Child destructor) would be a failure
  CHECK(*exited == 0);
  CHECK_FALSE(fs::exists(session));  // removed on clean shutdown
  std::error_code ec;
  fs::remove_all(dir, ec);
}
