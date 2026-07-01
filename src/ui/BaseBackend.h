#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

class ViewManager;

// Renderer/windowing seam for the viewer. ImGui is renderer-agnostic, so this
// interface is the only place a concrete GUI library plugs in: implement
// BaseBackend, expose a Create<Name>Backend() factory, and swap the call in
// main.cpp. Nothing outside src/ui/backends should include a backend's headers.
namespace UI {

// An ImTextureID, so it passes straight to ImGui::Image regardless of backend.
using TextureHandle = ImTextureID;
constexpr TextureHandle kInvalidTexture = (TextureHandle)0;

// A single point for DrawPointCloud (object-space position + RGBA color).
struct PreviewVertex {
    float position[3];
    unsigned char color[4];
};

// One display list of an assembled model (e.g. a flattened geo layout) with its
// object->world transform. Row-vector convention (v' = v * M), matching the
// N64: rotation in the upper 3x3, translation in row 3. `layer` is the SM64
// drawing layer (0-7), which selects the render mode (opaque/cutout/xlu).
struct ModelPart {
    std::string resource;
    float mtx[4][4];
    uint8_t layer = 1; // LAYER_OPAQUE
};

class BaseBackend {
public:
    virtual ~BaseBackend() = default;

    // Own the window + render loop end to end: create the window, apply the
    // theme, and pump frames (drawing `views` each frame) until the user closes
    // it, then tear everything down. Different backends drive the loop very
    // differently (raylib owns it directly; LUS draws our views from inside its
    // own frame), so the whole loop lives behind this one entry point.
    virtual void RunViewer(const std::shared_ptr<ViewManager>& views) = 0;

    // Upload 8-bit RGBA pixels; kInvalidTexture on failure. The backend owns the
    // GPU resource and releases it on teardown.
    virtual TextureHandle UploadRGBA8(const uint8_t* pixels, int width, int height) = 0;

    // Render a colored point cloud into an offscreen target and draw it at the
    // given screen rect via the current ImGui window. `id` keys a reusable
    // target so it is not recreated each frame; the view orbits the cloud's
    // bounds by yaw/pitch (radians) and zoom (>1 moves closer). Optional: a
    // backend without 3D support may leave this empty.
    virtual void DrawPointCloud(uint64_t id, const std::vector<PreviewVertex>& points,
                                const ImVec2& topLeft, const ImVec2& size,
                                float yaw, float pitch, float zoom) {}

    // Preview an N64 display-list resource (by its archive resource path) as a
    // shaded model, orbited by yaw/pitch (radians) and zoom, drawn at the given
    // screen rect. Requires the resource's archive to be mounted. Optional.
    virtual void DrawModel(const std::string& resourceName, const ImVec2& topLeft, const ImVec2& size,
                           float yaw, float pitch, float zoom) {}

    // Preview a multi-part model: each part is a display-list resource with its
    // own object->world transform (a geo layout flattened at bind pose). `key`
    // identifies the assembled model for render-target reuse. Optional.
    virtual void DrawModelParts(const std::string& key, const std::vector<ModelPart>& parts,
                                const ImVec2& topLeft, const ImVec2& size,
                                float yaw, float pitch, float zoom) {}
};

inline BaseBackend*& ActiveBackend() {
    static BaseBackend* backend = nullptr;
    return backend;
}

inline BaseBackend* GetBackend() {
    return ActiveBackend();
}

inline void SetBackend(BaseBackend* backend) {
    ActiveBackend() = backend;
}

} // namespace UI
