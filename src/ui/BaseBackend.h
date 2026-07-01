#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

class ViewManager;

// Renderer/windowing abstraction for the viewer. Implement BaseBackend and
// expose a Create<Name>Backend() factory; only src/ui/backends may include a
// backend's own headers.
namespace UI {

using TextureHandle = ImTextureID;
constexpr TextureHandle kInvalidTexture = (TextureHandle)0;

// A single point for DrawPointCloud (object-space position + RGBA color).
struct PreviewVertex {
    float position[3];
    unsigned char color[4];
};

// One display list of an assembled model, with its object->world transform
// (row-vector convention, translation in row 3) and SM64 drawing layer.
// Billboarded parts store their transform relative to the billboard node plus
// the node's world position; the backend rebuilds them facing the camera.
struct ModelPart {
    std::string resource;
    float mtx[4][4];
    uint8_t layer = 1; // LAYER_OPAQUE
    bool billboard = false;
    float anchor[3] = { 0.0f, 0.0f, 0.0f };
};

// Orbit camera: yaw/pitch in radians, zoom > 1 moves closer, pan in screen
// pixels shifting the look-at target in the view plane.
struct OrbitView {
    float yaw = 0.6f;
    float pitch = 0.3f;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
};

// Shared preview point light. Position is in bounding radii from the model's
// center; intensity falls off quadratically with distance. Parts that bind
// their own material lights (how SM64 colors body parts) override it.
struct PreviewLighting {
    bool enabled = true; // G_LIGHTING on/off
    float ambient[3] = { 0.25f, 0.25f, 0.25f };
    float color[3] = { 1.0f, 1.0f, 1.0f };
    float position[3] = { 1.5f, 2.0f, 1.5f };
    float intensity = 1.0f;
    float falloff = 0.3f;
};

inline PreviewLighting& GetPreviewLighting() {
    static PreviewLighting lighting;
    return lighting;
}

class BaseBackend {
public:
    virtual ~BaseBackend() = default;

    // Owns the window and render loop: creates the window, applies the theme,
    // draws `views` every frame until the user closes it.
    virtual void RunViewer(const std::shared_ptr<ViewManager>& views) = 0;

    // Uploads 8-bit RGBA pixels; kInvalidTexture on failure. The backend owns
    // the GPU resource.
    virtual TextureHandle UploadRGBA8(const uint8_t* pixels, int width, int height) = 0;

    // Draws a colored point cloud at the given screen rect. `id` keys a
    // reusable render target. Optional.
    virtual void DrawPointCloud(uint64_t id, const std::vector<PreviewVertex>& points,
                                const ImVec2& topLeft, const ImVec2& size,
                                float yaw, float pitch, float zoom) {}

    // Previews a display-list resource as a shaded model at the given screen
    // rect. Requires the resource's archive to be mounted. Optional.
    virtual void DrawModel(const std::string& resourceName, const ImVec2& topLeft, const ImVec2& size,
                           const OrbitView& view) {}

    // Previews a multi-part model (e.g. a flattened geo layout). `key`
    // identifies the assembled model for render-target reuse. Optional.
    virtual void DrawModelParts(const std::string& key, const std::vector<ModelPart>& parts,
                                const ImVec2& topLeft, const ImVec2& size, const OrbitView& view) {}
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
