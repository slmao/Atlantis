#pragma once

#include <atlantis/authoring/authoring_session.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/runtime/runtime_application.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace atlantis::runtime {

// Plan 0065 P5 (Spec 0065 R1, R2; ADR-0129 D2, D3; ADR-0131 D3, D5): the
// atlantis_runtime executable as the authoring session's host. Executable-
// private like editor_attachment.cpp: Atlantis::RuntimeHost does not link
// Atlantis Authoring.
//
// Where the build keeps the Runtime scene's source: the committed catalog
// source (`root=assets` entries resolve under assetsRoot) and the imported
// scenes the build made (their generated sources).
struct AuthoringSources {
  std::filesystem::path catalogSource;
  atlantis::authoring::SourceRoots roots;
};

// The session is created lazily, on the first connect() -- an atlantis_runtime
// that is never edited pays nothing (Plan 0065 J4). Its document is the
// Runtime's own scene; its sink is RuntimeApplication::requestSceneReload();
// afterFrame() turns a swapped reload into Applied{generation}. Frame thread
// only. Borrows the application: declared after it, destroyed before it, and
// every connection it handed out must be closed first.
class AuthoringHost {
 public:
  AuthoringHost(RuntimeApplication& application, AuthoringSources sources);
  ~AuthoringHost();
  AuthoringHost(const AuthoringHost&) = delete;
  AuthoringHost& operator=(const AuthoringHost&) = delete;

  // A new authoring connection; null (and an error logged once) when the
  // scene's source cannot be located.
  [[nodiscard]] std::unique_ptr<atlantis::connection::AuthoringConnection> connect();

  // After each runFrame(): reports a reload that swapped.
  void afterFrame();

  // The session, once created; null before the first connect().
  [[nodiscard]] atlantis::authoring::AuthoringSession* session() noexcept { return session_.get(); }

 private:
  RuntimeApplication& application_;
  AuthoringSources sources_;
  std::unique_ptr<atlantis::authoring::AuthoringSession> session_;
  bool unavailable_ = false;
  std::uint64_t generation_ = 0;
};

}  // namespace atlantis::runtime
