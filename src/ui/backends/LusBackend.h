#pragma once

#include <memory>

#include "ui/BaseBackend.h"

// libultraship-backed implementation of UI::BaseBackend (Fast3D renderer + SDL
// window + its bundled ImGui). Like the raylib factory, only this function is
// exposed so the concrete type and every LUS header stay in LusBackend.cpp.
namespace UI {

std::unique_ptr<BaseBackend> CreateLusBackend();

} // namespace UI
