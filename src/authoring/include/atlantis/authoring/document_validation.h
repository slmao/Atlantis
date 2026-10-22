#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/connection/authoring_connection.h>

#include <optional>

namespace atlantis::authoring {

// Plan 0065 P2 / ADR-0130 D1 (V-1): what makes an authoring document valid,
// the checks every transaction's result must pass. Two stages, each giving
// the first refusal or nothing:
//
// 1. checkSchema(): every field domain of sceneSchema() (FieldOutOfDomain),
//    then each of its constraints, in the schema's order, refused by its own
//    name. Driven by the schema: a constraint without a check fails the build.
// 2. checkCook(): the build's own cook of the document's text,
//    cookSceneToMemory() (Plan 0065 J2) -- every parser and cooker check,
//    and the round trip (NotRepresentableInSource). Anything it refuses that
//    stage 1 does not name is CookRefused.
//
// Pure functions of the document; safe for concurrent use.
[[nodiscard]] std::optional<connection::authoring::Error> checkSchema(
    const asset_system::scene::AuthoringScene& document);
[[nodiscard]] std::optional<connection::authoring::Error> checkCook(
    const asset_system::scene::AuthoringScene& document, const asset_system::AssetGuid& scene);
// checkSchema(), then checkCook(). `scene` must not be nil.
[[nodiscard]] std::optional<connection::authoring::Error> validateDocument(
    const asset_system::scene::AuthoringScene& document, const asset_system::AssetGuid& scene);

}  // namespace atlantis::authoring
