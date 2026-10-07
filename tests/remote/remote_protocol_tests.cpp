#include "remote_fixture.h"

#include "line_channel.h"
#include "os/socket.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

// Plan 0055 M2 (P2, P3; Spec 0055 non-functional robustness): the server's
// protocol behaviour, seen by a raw line client on the same loopback socket --
// pipelining, partial and oversize lines, the hello (token, protocol, missing),
// write-limit overrun, a client vanishing mid-request, and a poll that never
// waits.

namespace {

namespace json = atlantis::connection::json;
using atlantis::remote::LineChannel;
using atlantis::remote::RemoteServer;
using atlantis::remote::test::cookAndDecodeScene;
using atlantis::remote::test::kSceneSource;
using atlantis::remote::test::kToken;
using atlantis::remote::test::ServedWorld;

// A raw client: a LineChannel on a fresh loopback connection.
[[nodiscard]] LineChannel rawClient(const RemoteServer& server) {
  auto socket = atlantis::remote::os::connectLoopback(server.session().port, 5000);
  REQUIRE(socket.isOk());
  return LineChannel(std::move(socket.value()), 64 * 1024 * 1024, 64 * 1024 * 1024);
}

void sendRaw(LineChannel& client, std::string_view bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const auto result = atlantis::remote::os::send(client.socket(), bytes.substr(sent));
    REQUIRE(result.status != atlantis::remote::os::IoResult::Status::Closed);
    sent += result.bytes;
  }
}

[[nodiscard]] std::string request(std::uint64_t id, std::string_view method, std::string_view params = "{}") {
  return "{\"id\":" + std::to_string(id) + ",\"method\":\"" + std::string(method) + "\",\"params\":" +
         std::string(params) + "}\n";
}

[[nodiscard]] std::string hello(std::string_view token = kToken, std::string_view protocol = "atlantis.remote/1") {
  return request(0, "hello",
                 "{\"protocol\":\"" + std::string(protocol) + "\",\"token\":\"" + std::string(token) + "\"}");
}

// Reads whatever has arrived (waiting briefly for the loopback to deliver).
struct Received {
  std::vector<json::Value> responses;
  bool closed = false;
};
[[nodiscard]] Received receive(LineChannel& client, std::size_t expected,
                               std::chrono::milliseconds wait = std::chrono::seconds(5)) {
  Received out;
  const auto deadline = std::chrono::steady_clock::now() + wait;
  while (out.responses.size() < expected && std::chrono::steady_clock::now() < deadline) {
    (void)atlantis::remote::os::waitReadable(client.socket(), 20);
    std::vector<std::string> lines;
    const auto status = client.read(lines);
    for (const std::string& line : lines) out.responses.push_back(json::parse(line).value());
    if (status != LineChannel::ReadStatus::Ok) {
      out.closed = true;
      break;
    }
  }
  return out;
}

// Whether the server closed the connection (waits briefly).
[[nodiscard]] bool closedByServer(LineChannel& client) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    (void)atlantis::remote::os::waitReadable(client.socket(), 20);
    std::vector<std::string> lines;
    if (client.read(lines) != LineChannel::ReadStatus::Ok) return true;
  }
  return false;
}

[[nodiscard]] std::string errorCode(const json::Value& response) {
  const json::Value* error = response.find("error");
  if (error == nullptr) return {};
  return error->find("code")->asString();
}

}  // namespace

TEST_CASE("protocol: pipelined requests are all answered in one poll, in order", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  LineChannel client = rawClient(*world.server);
  std::string batch = hello();
  for (std::uint64_t id = 1; id <= 50; ++id) batch += request(id, "connection.listEntities");
  sendRaw(client, batch);
  // Let the loopback deliver everything, then one poll.
  (void)atlantis::remote::os::waitReadable(client.socket(), 50);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  world.server->poll();
  const Received received = receive(client, 51);
  REQUIRE(received.responses.size() == 51);
  for (std::uint64_t id = 0; id <= 50; ++id) {
    std::uint64_t got = 0;
    REQUIRE(received.responses[id].find("id")->toUInt64(got));
    CHECK(got == id);
    CHECK(received.responses[id].find("result") != nullptr);
  }
  CHECK(received.responses[1].find("result")->asArray().size() == 5);
}

TEST_CASE("protocol: a partial line waits for its end", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  LineChannel client = rawClient(*world.server);
  const std::string line = hello();
  sendRaw(client, line.substr(0, 20));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  world.server->poll();
  CHECK(receive(client, 1, std::chrono::milliseconds(200)).responses.empty());
  sendRaw(client, line.substr(20));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  world.server->poll();
  const Received received = receive(client, 1);
  REQUIRE(received.responses.size() == 1);
  CHECK(received.responses[0].find("result") != nullptr);
}

TEST_CASE("protocol: an oversize line disconnects the client", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  RemoteServer::Limits limits;
  limits.maxLineBytes = 1024;
  ServedWorld world(scene, limits);
  LineChannel client = rawClient(*world.server);
  sendRaw(client, std::string(2000, 'x'));  // no newline, over the limit
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  world.server->poll();
  CHECK(world.server->clientCount() == 0);
  CHECK(closedByServer(client));
}

TEST_CASE("protocol: a response over the write limit disconnects the client", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  RemoteServer::Limits limits;
  limits.maxPendingWriteBytes = 512;  // the schema is larger
  ServedWorld world(scene, limits);
  LineChannel client = rawClient(*world.server);
  sendRaw(client, hello() + request(1, "connection.schema"));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  world.server->poll();
  CHECK(world.server->clientCount() == 0);
}

TEST_CASE("protocol: a wrong token, a wrong protocol or a missing hello is refused and closed",
          "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  struct Case {
    std::string first;
    std::string code;
  };
  for (const Case& c : {Case{hello("ffffffffffffffffffffffffffffffff"), "BadToken"},
                        Case{hello(std::string(kToken).substr(0, 31)), "BadToken"},
                        Case{hello(kToken, "atlantis.remote/2"), "WrongProtocol"},
                        Case{request(7, "connection.listEntities"), "HelloRequired"}}) {
    INFO(c.first);
    LineChannel client = rawClient(*world.server);
    sendRaw(client, c.first + request(8, "connection.listEntities"));  // the second line is never answered
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    world.server->poll();
    world.server->poll();
    const Received received = receive(client, 2);
    REQUIRE(received.responses.size() == 1);
    CHECK(errorCode(received.responses[0]) == c.code);
    CHECK((received.closed || closedByServer(client)));
  }
  CHECK(world.server->clientCount() == 0);

  // connectRemote() reports the refusal.
  atlantis::remote::SessionInfo wrong = world.server->session();
  wrong.token = "ffffffffffffffffffffffffffffffff";
  atlantis::remote::RemoteOptions options;
  options.whileWaiting = [&] { world.server->poll(); };
  CHECK(atlantis::remote::connectRemote(wrong, options).error() == atlantis::remote::RemoteError::BadToken);
}

TEST_CASE("protocol: a malformed request gets an error and the session goes on", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  LineChannel client = rawClient(*world.server);
  sendRaw(client, hello() + "not json\n" + request(2, "connection.findEntity", R"({"entity":"bad"})") +
                      request(3, "connection.teleport") + request(4, "connection.listEntities"));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  world.server->poll();
  const Received received = receive(client, 5);
  REQUIRE(received.responses.size() == 5);
  CHECK(errorCode(received.responses[1]) == "BadRequest");
  CHECK(errorCode(received.responses[2]) == "BadRequest");
  CHECK(errorCode(received.responses[3]) == "BadRequest");
  CHECK(received.responses[4].find("result") != nullptr);
  CHECK(world.server->clientCount() == 1);
}

TEST_CASE("protocol: a client vanishing mid-request is dropped without harm", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  {
    LineChannel client = rawClient(*world.server);
    sendRaw(client, hello() + request(1, "connection.schema"));
  }  // closed before the server reads
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  for (int i = 0; i < 3; ++i) world.server->poll();
  CHECK(world.server->clientCount() == 0);
  const auto session = world.attach();  // the server still serves
  CHECK(session->connection().listEntities().size() == 5);
}

TEST_CASE("protocol: poll() never waits -- with no client, or an idle one", "[remote][protocol]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto timeThousandPolls = [&] {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) world.server->poll();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  };
  CHECK(timeThousandPolls() < 500);  // a wait of even 1 ms per poll would take a second
  const auto session = world.attach();
  CHECK(world.server->clientCount() == 1);
  CHECK(timeThousandPolls() < 500);
}
