// JSON representation of camera settings shared by the REST API and the web UI.
#pragma once

#include <string>
#include <string_view>

#include "core/camera_settings.hpp"

namespace core {

std::string camera_settings_to_json(const CameraSettings& settings);

// Applies a partial JSON object on top of `settings`. Unknown keys, wrong types or values out of
// range reject the whole update (settings is left untouched).
Result<CameraSettings> merge_camera_settings(const CameraSettings& settings, std::string_view json);

}  // namespace core
