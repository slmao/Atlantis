#include <atlantis/editor/model/viewport_picking.h>

#include <atlantis/connection/text.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
using atlantis::asset_system::EntityGuid;

std::optional<std::array<float, 2>> pixelOf(const CameraMatrices& camera, const PixelRect& rect, const Vec3& point) {
  const std::optional<std::array<float, 2>> ndc = projectToNdc(camera, point);
  if (!ndc.has_value()) return std::nullopt;
  return std::array<float, 2>{rect.x + ((*ndc)[0] + 1.0f) * 0.5f * rect.width,
                              rect.y + ((*ndc)[1] + 1.0f) * 0.5f * rect.height};
}

Ray rayThroughPixel(const CameraMatrices& camera, const PixelRect& rect, float px, float py) {
  const float ndcX = rect.width > 0.0f ? (px - rect.x) / rect.width * 2.0f - 1.0f : 0.0f;
  const float ndcY = rect.height > 0.0f ? (py - rect.y) / rect.height * 2.0f - 1.0f : 0.0f;
  return rayThroughNdc(camera, ndcX, ndcY);
}

std::optional<EntityGuid> pickMarker(std::span<const Marker> markers, const CameraMatrices& camera,
                                     const PixelRect& rect, float px, float py) {
  std::optional<EntityGuid> best;
  float bestDepth = 0.0f;
  for (const Marker& marker : markers) {
    const float depth = viewDepth(camera, marker.position);
    if (!(depth > 0.0f)) continue;
    const std::optional<std::array<float, 2>> pixel = pixelOf(camera, rect, marker.position);
    if (!pixel.has_value()) continue;
    const float dx = (*pixel)[0] - px;
    const float dy = (*pixel)[1] - py;
    if (dx * dx + dy * dy > kMarkerPickPixels * kMarkerPickPixels) continue;
    if (!best.has_value() || depth < bestDepth) {
      best = marker.entity;
      bestDepth = depth;
    }
  }
  return best;
}

// The component types and WorldMatrix.column3, found in the schema by name --
// the model names no World type.
ViewportPicking::ViewportPicking(connection::RuntimeConnection& connection) : connection_(connection) {
  for (const schema::TypeDescriptor& type : connection_.schema()) {
    const std::string_view name = connection::text::shortName(type.name);
    if (name == "Light") lightType_ = type.id;
    if (name == "Camera") cameraType_ = type.id;
    if (name == "Renderable") renderableType_ = type.id;
    if (name == "WorldMatrix") {
      for (const schema::FieldDescriptor& field : type.fields) {
        if (field.name == "column3") {
          worldMatrixType_ = type.id;
          column3Field_ = field.id;
        }
      }
    }
  }
}

void ViewportPicking::updateMarkers(const HierarchyModel& hierarchy) {
  markers_.clear();
  if (!worldMatrixType_.has_value()) return;
  const auto has = [](const HierarchyRow& row, const std::optional<schema::TypeId>& type) {
    return type.has_value() && std::find(row.components.begin(), row.components.end(), *type) != row.components.end();
  };
  for (const HierarchyRow& row : hierarchy.rows()) {
    if (has(row, renderableType_) || !has(row, worldMatrixType_)) continue;
    std::optional<MarkerKind> kind;
    if (has(row, lightType_)) {
      kind = MarkerKind::Light;
    } else if (has(row, cameraType_)) {
      kind = MarkerKind::Camera;
    }
    if (!kind.has_value()) continue;
    const auto value = connection_.getProperty(access::PropertyAddress{row.entity, *worldMatrixType_, column3Field_});
    const auto* column = value.isOk() ? std::get_if<std::array<float, 4>>(&value.value()) : nullptr;
    if (column == nullptr) continue;
    markers_.push_back(Marker{row.entity, *kind, Vec3{(*column)[0], (*column)[1], (*column)[2]}});
  }
}

void ViewportPicking::click(HierarchyModel& hierarchy, const CameraMatrices& camera, const PixelRect& rect, float px,
                            float py, bool withMarkers) {
  const std::optional<EntityGuid> marker =
      withMarkers ? pickMarker(markers_, camera, rect, px, py) : std::optional<EntityGuid>{};
  if (marker.has_value()) {
    hierarchy.select(*marker);
    request_.reset();
    outstandingId_.reset();
    return;
  }
  request_ = PickRequest{nextId_++, rayThroughPixel(camera, rect, px, py)};
  outstandingId_ = request_->id;
  outstandingGeneration_ = hierarchy.selectionGeneration();
}

bool ViewportPicking::answer(HierarchyModel& hierarchy, const PickAnswer& answer) {
  if (!outstandingId_.has_value() || answer.id != *outstandingId_) return false;
  outstandingId_.reset();
  if (hierarchy.selectionGeneration() != outstandingGeneration_) return false;
  hierarchy.select(answer.entity);
  return true;
}

}  // namespace atlantis::editor
