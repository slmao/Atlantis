#pragma once

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/result.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <variant>
#include <vector>

// Spec 0065 R1-R6, R9 / ADR-0129 D1-D4, ADR-0130 D2: Atlantis Authoring, the
// one owner of the document being edited. It depends on Atlantis Asset System
// (the document, its codecs, the scene schema, the cook) and Atlantis
// Connection (the interface it implements) only -- no Runtime, World ECS,
// Platform, RHI, Renderer or OS header -- so the host that creates it (the
// `atlantis_runtime` executable, ADR-0129 D2) is a deployment choice.

namespace atlantis::authoring {

// ------------------------------------------------------------ provenance

// The document is a hand-authored scene's committed source file: save()
// writes it back.
struct HandAuthored {
  std::filesystem::path sourcePath;
};
// The document is an imported scene's generated source (the glTF importer's
// output in the build): editable and applyable, never saved (ADR-0129 D4).
struct Imported {
  std::filesystem::path generatedSourcePath;
};
using Provenance = std::variant<HandAuthored, Imported>;

// What a session opens: the Runtime's scene (ADR-0129 D3, Spec 0065 Q6 P-a) --
// its AssetGuid and where its source text is.
struct DocumentSource {
  asset_system::AssetGuid scene;
  Provenance provenance;
};

// Where the build keeps scene sources, for locateDocument(): the assets root
// of `root=assets` catalog entries, and each imported scene the build made
// from a `gltf_import` entry (the host knows them from its build; Plan 0065
// P5).
struct ImportedScene {
  asset_system::AssetGuid importEntry;  // the gltf_import entry's GUID
  asset_system::AssetGuid scene;        // the imported scene's AssetGuid
  std::filesystem::path generatedSource;
};
struct SourceRoots {
  std::filesystem::path assetsRoot;
  std::vector<ImportedScene> imports;
};

enum class LocateError : std::uint8_t {
  NotASceneEntry,   // neither a scene nor a gltf_import entry
  UnsupportedRoot,  // a scene entry outside the assets root
  ImportNotBuilt,   // a gltf_import entry the build made no scene from
};

[[nodiscard]] std::string_view toString(LocateError error) noexcept;

// A catalog entry's document source: a `scene` entry under the assets root
// is HandAuthored at assetsRoot/path; a `gltf_import` entry is the Imported
// scene the build made from it.
[[nodiscard]] atlantis::Result<DocumentSource, LocateError> locateDocument(
    const asset_system::CatalogSourceEntry& entry, const SourceRoots& roots);

// ---------------------------------------------------------------- session

// At most one open document (ADR-0129 D1): the scene `source` names, read by
// open(). Every client reaches it through a connection(); commands apply to a
// copy of the document, which is validated (validateDocument()) and swapped in
// only if valid (V-1), and every change is reported to every matching
// subscription of every connection, in application order.
//
// Not thread-safe (ADR-0004): the host calls it, and its connections, on its
// frame thread between frames. Connections borrow the session and must not
// outlive it -- its destructor CHECKs that every one has been closed. Not
// copyable or movable, since connections hold its address.
class AuthoringSession {
 public:
  // `source.scene` must not be nil. No document is open until a connection's
  // open() succeeds.
  explicit AuthoringSession(DocumentSource source);
  ~AuthoringSession();
  AuthoringSession(const AuthoringSession&) = delete;
  AuthoringSession& operator=(const AuthoringSession&) = delete;
  AuthoringSession(AuthoringSession&&) = delete;
  AuthoringSession& operator=(AuthoringSession&&) = delete;

  // The InProcess endpoint (ADR-0130 D2): a new connection, closed when the
  // returned object is destroyed.
  [[nodiscard]] std::unique_ptr<connection::AuthoringConnection> connect();

  [[nodiscard]] const DocumentSource& source() const noexcept;
  // The open document, read-only, for the host; nullptr until open().
  [[nodiscard]] const asset_system::scene::AuthoringScene* document() const noexcept;

  // The host reports that the Runtime swapped in an applied document (Plan
  // 0065 P4, ADR-0131 D5): every subscription receives Applied{generation}.
  void reportApplied(std::uint64_t generation);

  struct State;  // private to authoring_session.cpp

 private:
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::authoring
