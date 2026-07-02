#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
// Texture bound to a model part; the backend emits the tile setup. Offsets
// index into blob; fmt/siz are G_IM_FMT_*/G_IM_SIZ_* values, cm* are
// G_TX_WRAP/MIRROR/CLAMP flags.
struct PartTexture {
    std::shared_ptr<std::vector<uint8_t>> blob;
    uint32_t rasterOffset = 0;
    uint32_t paletteOffset = 0; // CI formats only
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fmt = 0;
    uint8_t siz = 0;
    uint8_t cmS = 0;
    uint8_t cmT = 0;
    // Cycle-1 combine: 0 modulate, 1 blend, 2 decal (PM64 main-only subtypes).
    uint8_t combine = 0;
    // Second tile blended into cycle 1 (PM64 aux textures): 0 = none,
    // 2 = shared raster (aux is the second half of the main raster),
    // 3 = independent raster/palette.
    uint8_t auxMode = 0;
    uint32_t auxRasterOffset = 0;
    uint32_t auxPaletteOffset = 0;
    uint16_t auxWidth = 0;
    uint16_t auxHeight = 0;
    uint8_t auxFmt = 0;
    uint8_t auxSiz = 0;
    uint8_t auxCmS = 0;
    uint8_t auxCmT = 0;
};

struct ModelPart {
    std::string resource;
    float mtx[4][4];
    uint8_t layer = 1; // LAYER_OPAQUE
    // Exact render mode pair (othermode L cycle words); 0 = layer default.
    uint32_t renderMode1 = 0;
    uint32_t renderMode2 = 0;
    uint8_t cycleType = 1; // pipeline cycles the render mode pair expects (1 or 2)
    bool billboard = false;
    float anchor[3] = { 0.0f, 0.0f, 0.0f };
    bool unlit = false; // vertex-colored geometry; preview lighting must not apply
    std::shared_ptr<PartTexture> texture;
};

// A display-list bundle parsed from a game blob: byte-swapped f3dex2 command
// words whose pointer operands are file offsets. G_VTX operands are relative
// to vtxBase within the blob; G_DL operands reference other dlists by their
// blob offset; G_SETTIMG operands are blob offsets.
struct GfxBundleDList {
    uint32_t offset = 0;
    std::vector<uint32_t> words; // w0/w1 pairs
};

struct GfxBundle {
    std::shared_ptr<std::vector<uint8_t>> blob;
    uint32_t vtxBase = 0;
    uint32_t vtxSize = 0;
    std::vector<GfxBundleDList> dlists;
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

// Shared preview fog (N64 vertex fog: blender cycle-1 fog shade). Start/end
// are gSPFogPosition units (0..1000 across the near..far range).
struct PreviewAtmosphere {
    bool fogEnabled = false;
    float fogColor[3] = { 0.45f, 0.50f, 0.65f };
    int fogStart = 890;
    int fogEnd = 1000;
};

inline PreviewAtmosphere& GetPreviewAtmosphere() {
    static PreviewAtmosphere atmosphere = [] {
        PreviewAtmosphere a;
        if (const char* f = std::getenv("TORCH_UI_FOG")) {
            a.fogEnabled = true;
            int s0 = 0, e0 = 0;
            if (sscanf(f, "%d:%d", &s0, &e0) == 2 && e0 > s0) {
                a.fogStart = s0;
                a.fogEnd = e0;
            }
        }
        return a;
    }();
    return atmosphere;
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
                                const ImVec2& topLeft, const ImVec2& size, const OrbitView& view) {}

    // Previews a display-list resource as a shaded model at the given screen
    // rect. Requires the resource's archive to be mounted. Optional.
    virtual void DrawModel(const std::string& resourceName, const ImVec2& topLeft, const ImVec2& size,
                           const OrbitView& view) {}

    // Previews a multi-part model (e.g. a flattened geo layout). `key`
    // identifies the assembled model for render-target reuse. Optional.
    virtual void DrawModelParts(const std::string& key, const std::vector<ModelPart>& parts,
                                const ImVec2& topLeft, const ImVec2& size, const OrbitView& view) {}

    // Registers an entry display list from a parsed game blob under `name` so
    // ModelPart::resource can reference it. Returns false if unsupported.
    virtual bool RegisterGameDList(const std::string& name, const GfxBundle& bundle, uint32_t entryOffset) {
        return false;
    }

    // Screen-space backdrop image (RGBA8, cover-fit) drawn behind the model
    // keyed by DrawModelParts' `key`. nullptr clears it. Optional.
    virtual void SetPreviewBackdrop(const std::string& key, const uint8_t* rgba, int width, int height) {}

    // Renders a colored triangle soup (three PreviewVertex per triangle) with
    // the same camera and render-target reuse as DrawModelParts. Optional.
    virtual void DrawTriangles(const std::string& key, const std::vector<PreviewVertex>& tris,
                               const ImVec2& topLeft, const ImVec2& size, const OrbitView& view) {}

    // Plays interleaved 16-bit PCM, replacing whatever is currently playing.
    virtual bool PlaySamples(const int16_t* frames, size_t frameCount, int sampleRate, int channels) { return false; }
    virtual void StopAudio() {}
    // Playback position in [0,1]; negative when idle/finished.
    virtual float AudioProgress() { return -1.0f; }
    virtual void SeekAudio(float progress) {}
    virtual void SetAudioVolume(float volume) {}
    virtual float GetAudioVolume() { return 1.0f; }
    // Playback-rate multiplier (1 = the sample's own rate).
    virtual void SetAudioSpeed(float speed) {}
    virtual float GetAudioSpeed() { return 1.0f; }
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
