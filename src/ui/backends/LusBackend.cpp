#ifdef BUILD_UI

#include "ui/backends/LusBackend.h"

#include <filesystem>
#include <vector>

#include "ui/Theme.h"
#include "ui/View.h"

#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h>
#include <fast/Fast3dWindow.h>
#include <fast/Fast3dGui.h>
#include <fast/interpreter.h>
#include <fast/backends/gfx_rendering_api.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Vertex.h>
#include <fast/resource/ResourceType.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/LightFactory.h>
#include <fast/types.h>
#include <fast/lus_gbi.h>
#include <ship/resource/ResourceManager.h>
#include <ship/audio/Audio.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/File.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <libultraship/libultra/gbi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

// The only translation unit that touches libultraship.
namespace UI {

namespace {

// Hosts the Torch views inside LUS's gui draw loop. Draw() is overridden to
// drive the active View directly; the View owns its own fullscreen window.
class ViewHostWindow : public Ship::GuiWindow {
public:
    explicit ViewHostWindow(std::shared_ptr<ViewManager> views)
        : Ship::GuiWindow("gTorchViewer", true, "Torch"), mViews(std::move(views)) {}

    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}

    void Draw() override {
        // Single OS window: no platform viewports or docking.
        ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_DockingEnable);
        if (mViews != nullptr) {
            mViews->Render();
        }
    }

private:
    std::shared_ptr<ViewManager> mViews;
};

// Minimal control deck: the viewer takes no game input, but Context requires a
// non-null deck. WriteToPad is a no-op.
class ViewerControlDeck final : public Ship::ControlDeck {
public:
    ViewerControlDeck()
        : Ship::ControlDeck({}, std::make_shared<Ship::ControllerDefaultMappings>(), {}) {}
    void WriteToPad(void*) override {}
};

// Suppress Fast3dGui's fullscreen "Main Game" window; there is no game scene
// and it would cover the viewer.
class ViewerGui final : public Fast::Fast3dGui {
public:
    using Fast::Fast3dGui::Fast3dGui;
    void DrawGame() override {}
};

class LusBackend final : public BaseBackend {
public:
    void RunViewer(const std::shared_ptr<ViewManager>& views) override {
        auto ctx = Ship::Context::CreateUninitializedInstance("Torch", "torch", "torch.cfg.json");
        ctx->InitConfiguration();
        ctx->InitConsoleVariables();
        ctx->InitLogging();
        ctx->InitControlDeck(std::make_shared<ViewerControlDeck>());

        // Mount the .o2r archives from the working directory so Fast3D can
        // resolve the resources referenced by the previewed assets.
        std::vector<std::string> archives;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path(), ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".o2r") {
                archives.push_back(entry.path().string());
            }
        }
#ifdef TORCH_LUS_SHADER_DIR
        // Mount the upstream LUS shaders last so they override any fork-modified
        // shaders shipped inside the game archives (last archive added wins).
        if (std::filesystem::exists(TORCH_LUS_SHADER_DIR)) {
            archives.push_back(TORCH_LUS_SHADER_DIR);
        }
#endif
        ctx->InitResourceManager(archives, {}, 1);
        ctx->InitConsole();
        ctx->InitAudio(Ship::AudioSettings{});

        // Fast3D binary resource factories (DisplayList/Vertex/Texture/Matrix/Light).
        auto loader = ctx->GetResourceManager()->GetResourceLoader();
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                        RESOURCE_FORMAT_BINARY, "DisplayList",
                                        static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(),
                                        RESOURCE_FORMAT_BINARY, "Vertex",
                                        static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(),
                                        RESOURCE_FORMAT_BINARY, "Texture",
                                        static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(),
                                        RESOURCE_FORMAT_BINARY, "Texture",
                                        static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(),
                                        RESOURCE_FORMAT_BINARY, "Matrix",
                                        static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
        loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryLightV0>(),
                                        RESOURCE_FORMAT_BINARY, "Light",
                                        static_cast<uint32_t>(Fast::ResourceType::Light), 0);

        // GuiWindows must be added after InitWindow; their setup dereferences
        // Context::GetWindow().
        auto gui = std::make_shared<ViewerGui>(std::vector<std::shared_ptr<Ship::GuiWindow>>{});
        auto window = std::make_shared<Fast::Fast3dWindow>(gui);
        ctx->InitWindow(window);
        ctx->InitEventSystem();

        window->GetGui()->AddGuiWindow(std::make_shared<ViewHostWindow>(views));

        window->SetTargetFps(60);
        // MSAA > 1 forces Fast3D into the offscreen game framebuffer
        // (mRendersToFb), which the previews rely on.
        window->SetMsaaLevel(4);

        // SM64 display lists are F3D; f3dex2 would misread the opcodes.
        window->SetRendererUCode(ucode_f3d);

        ApplyTorchTheme();

        static Gfx emptyDl[] = { gsSPEndDisplayList() };
        while (window->IsRunning()) {
            window->HandleEvents();
            PumpAudio();

            // Render the previous frame's preview requests; the gui draw blits
            // the results and queues the next batch.
            mRenderList.swap(mRequests);
            mRequests.clear();
            Gfx* commands = BuildModelCommands(window);
            if (commands != nullptr) {
                window->DrawAndRunGraphicsCommands(commands, mMtxReplacements);
            } else {
                window->DrawAndRunGraphicsCommands(emptyDl, {});
            }
        }
    }

    TextureHandle UploadRGBA8(const uint8_t* pixels, int width, int height) override {
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return kInvalidTexture;
        }
        // Mirrors Fast3dGui::LoadGuiTexture's upload path.
        auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
        if (window == nullptr) {
            return kInvalidTexture;
        }
        auto interpreter = window->GetInterpreterWeak().lock();
        if (interpreter == nullptr) {
            return kInvalidTexture;
        }
        auto* api = interpreter->GetCurrentRenderingAPI();
        if (api == nullptr) {
            return kInvalidTexture;
        }

        const uint32_t id = api->NewTexture();
        api->SelectTexture(0, id);
        api->SetSamplerParameters(0, false, 0, 0); // nearest + clamp: crisp N64 texels
        api->UploadTexture(pixels, (uint32_t)width, (uint32_t)height);
        return api->GetTextureById((int)id);
    }

    void DrawPointCloud(uint64_t id, const std::vector<PreviewVertex>& points, const ImVec2& topLeft,
                        const ImVec2& size, const OrbitView& view) override {
        if (points.empty()) {
            return;
        }
        // Octahedron marker per point, sized against the cloud's bounds.
        float mn[3] = { 1e18f, 1e18f, 1e18f }, mx[3] = { -1e18f, -1e18f, -1e18f };
        for (const auto& pt : points) {
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], pt.position[k]);
                mx[k] = std::max(mx[k], pt.position[k]);
            }
        }
        const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
        const float r = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) * 0.008f, 1.0f);

        static const int kFaces[8][3] = {
            { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 },
            { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 },
        };
        constexpr size_t kMaxPoints = 4000;
        std::vector<PreviewVertex> tris;
        tris.reserve(std::min(points.size(), kMaxPoints) * 24);
        for (size_t i = 0; i < points.size() && i < kMaxPoints; ++i) {
            const auto& pt = points[i];
            const float c[6][3] = {
                { pt.position[0] + r, pt.position[1], pt.position[2] },
                { pt.position[0] - r, pt.position[1], pt.position[2] },
                { pt.position[0], pt.position[1], pt.position[2] + r },
                { pt.position[0], pt.position[1], pt.position[2] - r },
                { pt.position[0], pt.position[1] + r, pt.position[2] },
                { pt.position[0], pt.position[1] - r, pt.position[2] },
            };
            for (const auto& face : kFaces) {
                for (int k = 0; k < 3; ++k) {
                    PreviewVertex v = pt;
                    std::memcpy(v.position, c[face[k]], sizeof(v.position));
                    tris.push_back(v);
                }
            }
        }
        // Thin ribbons between consecutive vertices; the buffer is laid out in
        // triangle order, so the links trace the mesh structure.
        const float lw = r * 0.35f;
        for (size_t i = 0; i + 1 < points.size() && i + 1 < kMaxPoints; ++i) {
            const float* a = points[i].position;
            const float* b = points[i + 1].position;
            float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 0.0001f) {
                continue;
            }
            unsigned char color[4];
            for (int k = 0; k < 4; ++k) {
                color[k] = (unsigned char)((points[i].color[k] + points[i + 1].color[k]) / 3);
            }
            color[3] = 255;
            float u[3] = { d[2] / len * lw, 0.0f, -d[0] / len * lw };
            if (std::fabs(u[0]) + std::fabs(u[2]) < 0.0001f) {
                u[0] = lw;
            }
            const float v2[3] = { (d[1] * u[2] - d[2] * u[1]) / len, (d[2] * u[0] - d[0] * u[2]) / len,
                                  (d[0] * u[1] - d[1] * u[0]) / len };
            const float* offs[2] = { u, v2 };
            for (const auto* o : offs) {
                const float q0[3] = { a[0] - o[0], a[1] - o[1], a[2] - o[2] };
                const float q1[3] = { a[0] + o[0], a[1] + o[1], a[2] + o[2] };
                const float q2[3] = { b[0] + o[0], b[1] + o[1], b[2] + o[2] };
                const float q3[3] = { b[0] - o[0], b[1] - o[1], b[2] - o[2] };
                const float* quads[2][3] = { { q0, q1, q2 }, { q0, q2, q3 } };
                for (const auto& q : quads) {
                    for (int k = 0; k < 3; ++k) {
                        PreviewVertex v{};
                        std::memcpy(v.position, q[k], sizeof(v.position));
                        std::memcpy(v.color, color, 4);
                        tris.push_back(v);
                    }
                }
            }
        }
        DrawTriangles("points://" + std::to_string(id), tris, topLeft, size, view);
    }

    void DrawTriangles(const std::string& key, const std::vector<PreviewVertex>& tris, const ImVec2& topLeft,
                       const ImVec2& size, const OrbitView& view) override {
        if (tris.size() < 3) {
            return;
        }
        RawModel& raw = mRawModels[key];
        const uint64_t hash = HashBytes(tris.data(), tris.size() * sizeof(PreviewVertex));
        if (raw.hash != hash) {
            BuildRawModel(raw, tris, hash);
            mBoundsCache[key] = raw.bounds;
            for (auto& slot : mFbPool) {
                if (slot.owner == key) {
                    slot.rendered = false;
                }
            }
        }

        ModelPart part;
        part.resource = key;
        static const float kIdentity[4][4] = {
            { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
        };
        std::memcpy(part.mtx, kIdentity, sizeof(kIdentity));
        DrawModelParts(key, { part }, topLeft, size, view);
    }

    void DrawModel(const std::string& resourceName, const ImVec2& topLeft, const ImVec2& size,
                   const OrbitView& view) override {
        // A single display list is just a one-part model with an identity transform.
        static const float kIdentity[4][4] = {
            { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
        };
        ModelPart part;
        part.resource = resourceName;
        std::memcpy(part.mtx, kIdentity, sizeof(kIdentity));
        DrawModelParts(resourceName, { part }, topLeft, size, view);
    }

    void DrawModelParts(const std::string& key, const std::vector<ModelPart>& parts, const ImVec2& topLeft,
                        const ImVec2& size, const OrbitView& view) override {
        if (size.x < 1.0f || size.y < 1.0f || parts.empty()) {
            return;
        }
        // Queue for next frame's render pass, then blit this model's framebuffer
        // (sized to its rect, so it fills the canvas centered).
        mRequests.push_back({ key, parts, topLeft, size, view });

        auto it = mNameToFb.find(key);
        if (it == mNameToFb.end()) {
            return;
        }
        auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
        if (window == nullptr) {
            return;
        }
        auto interp = window->GetInterpreterWeak().lock();
        if (interp == nullptr) {
            return;
        }
        auto* api = interp->GetCurrentRenderingAPI();
        if (api == nullptr) {
            return;
        }
        void* tex = api->GetFramebufferTextureId(it->second);
        if (tex == nullptr) {
            return;
        }
        // Metal/D3D render targets are top-down (match ImGui), OpenGL bottom-up.
        const bool flipV = api->GetClipParameters().invertY;
        const ImVec2 uvMin(0.0f, flipV ? 1.0f : 0.0f);
        const ImVec2 uvMax(1.0f, flipV ? 0.0f : 1.0f);
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(tex), topLeft,
                                             ImVec2(topLeft.x + size.x, topLeft.y + size.y), uvMin, uvMax);
    }

    bool PlaySamples(const int16_t* frames, size_t frameCount, int sampleRate, int channels) override {
        if (frames == nullptr || frameCount == 0 || sampleRate <= 0 || channels <= 0) {
            return false;
        }
        auto audio = Ship::Context::GetInstance()->GetAudio();
        if (audio == nullptr || audio->GetAudioPlayer() == nullptr ||
            !audio->GetAudioPlayer()->IsInitialized()) {
            return false;
        }
        mAudioPcm.assign(frames, frames + frameCount * channels);
        mAudioSrcRate = sampleRate;
        mAudioSrcChannels = channels;
        mAudioTotalFrames = frameCount;
        mAudioPos = 0.0;
        return true;
    }

    void StopAudio() override {
        mAudioPcm.clear();
        mAudioTotalFrames = 0;
        mAudioPos = 0.0;
    }

    float AudioProgress() override {
        if (mAudioTotalFrames == 0) {
            return -1.0f;
        }
        return (float)(mAudioPos / (double)mAudioTotalFrames);
    }

    void SeekAudio(float progress) override {
        if (mAudioTotalFrames != 0) {
            mAudioPos = std::clamp((double)progress, 0.0, 1.0) * (double)mAudioTotalFrames;
        }
    }

    void SetAudioVolume(float volume) override {
        mAudioVolume = std::clamp(volume, 0.0f, 1.0f);
    }

    float GetAudioVolume() override {
        return mAudioVolume;
    }

    void SetAudioSpeed(float speed) override {
        mAudioSpeed = std::clamp(speed, 0.25f, 4.0f);
    }

    float GetAudioSpeed() override {
        return mAudioSpeed;
    }

private:
    // out = a * b in row-vector convention (a applied first).
    static void MulMtxF(MtxF& out, const float a[4][4], const MtxF& b) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                out.mf[i][j] = a[i][0] * b.mf[0][j] + a[i][1] * b.mf[1][j] + a[i][2] * b.mf[2][j] +
                               a[i][3] * b.mf[3][j];
            }
        }
    }

    // Row-vector (v' = v*M) perspective + look-at, matching N64/Fast3D convention.
    static void Perspective(MtxF& m, float fovyDeg, float aspect, float n, float f) {
        for (auto& row : m.mf) {
            for (auto& v : row) {
                v = 0.0f;
            }
        }
        const float cot = 1.0f / std::tan(fovyDeg * (float)M_PI / 360.0f);
        m.mf[0][0] = cot / aspect;
        m.mf[1][1] = cot;
        m.mf[2][2] = (n + f) / (n - f);
        m.mf[2][3] = -1.0f;
        m.mf[3][2] = 2.0f * n * f / (n - f);
    }

    static void LookAt(MtxF& m, float ex, float ey, float ez, float ax, float ay, float az) {
        float zx = ex - ax, zy = ey - ay, zz = ez - az;
        float zl = std::sqrt(zx * zx + zy * zy + zz * zz);
        if (zl == 0.0f) {
            zl = 1.0f;
        }
        zx /= zl; zy /= zl; zz /= zl;
        // x = up(0,1,0) x z, normalized
        float xx = zz, xy = 0.0f, xz = -zx;
        float xl = std::sqrt(xx * xx + xy * xy + xz * xz);
        if (xl == 0.0f) {
            xl = 1.0f;
        }
        xx /= xl; xy /= xl; xz /= xl;
        // y = z x x
        const float yx = zy * xz - zz * xy;
        const float yy = zz * xx - zx * xz;
        const float yz = zx * xy - zy * xx;
        m.mf[0][0] = xx; m.mf[0][1] = yx; m.mf[0][2] = zx; m.mf[0][3] = 0.0f;
        m.mf[1][0] = xy; m.mf[1][1] = yy; m.mf[1][2] = zy; m.mf[1][3] = 0.0f;
        m.mf[2][0] = xz; m.mf[2][1] = yz; m.mf[2][2] = zz; m.mf[2][3] = 0.0f;
        m.mf[3][0] = -(xx * ex + xy * ey + xz * ez);
        m.mf[3][1] = -(yx * ex + yy * ey + yz * ez);
        m.mf[3][2] = -(zx * ex + zy * ey + zz * ez);
        m.mf[3][3] = 1.0f;
    }

    // Accumulate vertex bounds across a display list, following vertex loads
    // (G_VTX_OTR_HASH) and sub-DL branches (G_DL_OTR_HASH). The resource hash
    // sits in the Gfx word after the command.
    void AccumulateBounds(const std::shared_ptr<Fast::DisplayList>& dl,
                          const std::shared_ptr<Ship::ResourceManager>& rm, float mn[3], float mx[3], bool& any,
                          std::unordered_set<std::string>& visited, int depth) {
        if (dl == nullptr || depth > 24) {
            return;
        }
        auto am = rm->GetArchiveManager();
        const auto& instr = dl->Instructions;
        for (size_t i = 0; i + 1 < instr.size(); ++i) {
            const int8_t op = (int8_t)(instr[i].words.w0 >> 24);
            const bool isVtx = op == (int8_t)0x32;
            const bool isDl = op == (int8_t)0x31;
            if (!isVtx && !isDl) {
                continue;
            }
            const uint64_t hash = ((uint64_t)(uint32_t)instr[i + 1].words.w0 << 32) |
                                  (uint32_t)instr[i + 1].words.w1;
            ++i; // the hash occupies the next Gfx word
            const std::string* path = am ? am->HashToString(hash) : nullptr;
            if (path == nullptr || !visited.insert(*path).second) {
                continue;
            }
            if (isVtx) {
                auto vtx = std::static_pointer_cast<Fast::Vertex>(rm->LoadResource(*path));
                if (vtx == nullptr) {
                    continue;
                }
                for (const auto& v : vtx->VertexList) {
                    for (int k = 0; k < 3; ++k) {
                        const float val = (float)v.v.ob[k];
                        mn[k] = std::min(mn[k], val);
                        mx[k] = std::max(mx[k], val);
                    }
                    any = true;
                }
            } else { // branch into the sub-display-list
                auto sub = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(*path));
                AccumulateBounds(sub, rm, mn, mx, any, visited, depth + 1);
            }
        }
    }

    // Vertex bounds of a display list, used to frame it (centered, fit to view).
    struct ModelBounds {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f, radius = 150.0f;
    };

    ModelBounds ComputeBounds(const std::shared_ptr<Fast::DisplayList>& dl,
                              const std::shared_ptr<Ship::ResourceManager>& rm) {
        ModelBounds b;
        if (dl == nullptr) {
            return b;
        }
        float mn[3] = { 1e18f, 1e18f, 1e18f };
        float mx[3] = { -1e18f, -1e18f, -1e18f };
        bool any = false;
        std::unordered_set<std::string> visited;
        AccumulateBounds(dl, rm, mn, mx, any, visited, 0);
        if (any) {
            b.cx = (mn[0] + mx[0]) * 0.5f;
            b.cy = (mn[1] + mx[1]) * 0.5f;
            b.cz = (mn[2] + mx[2]) * 0.5f;
            const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
            b.radius = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0f);
        }
        if (!any || b.radius > 50000.0f || !std::isfinite(b.radius)) {
            b = ModelBounds{};
        }
        return b;
    }

    // Union of each part's bounds transformed by its matrix.
    ModelBounds ComputePartsBounds(const std::vector<ModelPart>& parts,
                                   const std::shared_ptr<Ship::ResourceManager>& rm) {
        float mn[3] = { 1e18f, 1e18f, 1e18f };
        float mx[3] = { -1e18f, -1e18f, -1e18f };
        bool any = false;
        for (const auto& part : parts) {
            auto bit = mBoundsCache.find(part.resource);
            if (bit == mBoundsCache.end()) {
                auto dl = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(part.resource));
                if (dl == nullptr) {
                    continue;
                }
                bit = mBoundsCache.emplace(part.resource, ComputeBounds(dl, rm)).first;
            }
            const ModelBounds& pb = bit->second;
            const auto& m = part.mtx;
            // Billboard matrices are anchor-relative; shift by the anchor.
            const float ax = part.billboard ? part.anchor[0] : 0.0f;
            const float ay = part.billboard ? part.anchor[1] : 0.0f;
            const float az = part.billboard ? part.anchor[2] : 0.0f;
            const float cx = pb.cx * m[0][0] + pb.cy * m[1][0] + pb.cz * m[2][0] + m[3][0] + ax;
            const float cy = pb.cx * m[0][1] + pb.cy * m[1][1] + pb.cz * m[2][1] + m[3][1] + ay;
            const float cz = pb.cx * m[0][2] + pb.cy * m[1][2] + pb.cz * m[2][2] + m[3][2] + az;
            float scale = 0.0f;
            for (int r = 0; r < 3; ++r) {
                scale = std::max(scale, std::sqrt(m[r][0] * m[r][0] + m[r][1] * m[r][1] + m[r][2] * m[r][2]));
            }
            const float radius = pb.radius * std::max(scale, 0.001f);
            mn[0] = std::min(mn[0], cx - radius); mx[0] = std::max(mx[0], cx + radius);
            mn[1] = std::min(mn[1], cy - radius); mx[1] = std::max(mx[1], cy + radius);
            mn[2] = std::min(mn[2], cz - radius); mx[2] = std::max(mx[2], cz + radius);
            any = true;
        }
        ModelBounds b;
        if (any) {
            b.cx = (mn[0] + mx[0]) * 0.5f;
            b.cy = (mn[1] + mx[1]) * 0.5f;
            b.cz = (mn[2] + mx[2]) * 0.5f;
            const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
            b.radius = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0f);
        }
        if (!any || b.radius > 50000.0f || !std::isfinite(b.radius)) {
            b = ModelBounds{};
        }
        return b;
    }

    // Render target for one model preview, pooled and reassigned by name as
    // rows scroll.
    struct FbSlot {
        int fbId = -1;
        uint32_t w = 0, h = 0;
        std::string owner; // model currently rendered into this framebuffer
        Vp vp{};
        Mtx proj{};        // stable address: key into the Mtx->MtxF replacement map
        // Last rendered state; the framebuffer persists until these change.
        bool rendered = false;
        OrbitView lastView{};
        uint64_t lastPartsHash = 0;
    };

    static bool SameView(const OrbitView& a, const OrbitView& b) {
        return a.yaw == b.yaw && a.pitch == b.pitch && a.zoom == b.zoom && a.panX == b.panX && a.panY == b.panY;
    }

    static uint64_t HashBytes(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    // In-memory geometry for DrawTriangles: vertex-colored, unlit.
    struct RawModel {
        std::vector<Fast::F3DVtx> vtx;
        std::vector<Gfx> gfx;
        ModelBounds bounds;
        uint64_t hash = 0;
    };

    void BuildRawModel(RawModel& raw, const std::vector<PreviewVertex>& tris, uint64_t hash) {
        static_assert(sizeof(Fast::F3DVtx) == 16, "F3D vertex encoding assumes 16-byte vertices");
        const size_t triCount = tris.size() / 3;
        raw.hash = hash;
        raw.vtx.clear();
        raw.gfx.clear();
        raw.vtx.reserve(triCount * 3);

        float mn[3] = { 1e18f, 1e18f, 1e18f }, mx[3] = { -1e18f, -1e18f, -1e18f };
        for (size_t i = 0; i < triCount * 3; ++i) {
            const PreviewVertex& src = tris[i];
            Fast::F3DVtx v{};
            for (int k = 0; k < 3; ++k) {
                v.v.ob[k] = (short)std::clamp(src.position[k], -32768.0f, 32767.0f);
                mn[k] = std::min(mn[k], src.position[k]);
                mx[k] = std::max(mx[k], src.position[k]);
            }
            std::memcpy(v.v.cn, src.color, 4);
            raw.vtx.push_back(v);
        }
        raw.bounds.cx = (mn[0] + mx[0]) * 0.5f;
        raw.bounds.cy = (mn[1] + mx[1]) * 0.5f;
        raw.bounds.cz = (mn[2] + mx[2]) * 0.5f;
        const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
        raw.bounds.radius = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0f);

        // Vertex colors: no lighting, shade-only combiner. F3D loads at most 16
        // vertices, so batch 5 triangles per load (raw F3D G_VTX/G_TRI1 words;
        // the gbi.h macros emit F3DEX encodings the f3d handlers misread).
        raw.gfx.reserve(triCount * 3 + 8);
        Gfx g{};
        gSPClearGeometryMode(&g, G_LIGHTING);
        raw.gfx.push_back(g);
        gDPSetCombineMode(&g, G_CC_SHADE, G_CC_SHADE);
        raw.gfx.push_back(g);
        for (size_t base = 0; base < triCount * 3; base += 15) {
            const size_t n = std::min<size_t>(15, triCount * 3 - base);
            g.words.w0 = ((uintptr_t)0x04 << 24) | (uintptr_t)(n * sizeof(Fast::F3DVtx)); // G_VTX, v0=0
            g.words.w1 = (uintptr_t)&raw.vtx[base];
            raw.gfx.push_back(g);
            for (size_t t = 0; t + 2 < n; t += 3) {
                g.words.w0 = (uintptr_t)0xBF << 24; // G_TRI1, indices * 10
                g.words.w1 = ((uintptr_t)(t * 10) << 16) | ((uintptr_t)((t + 1) * 10) << 8) | (uintptr_t)((t + 2) * 10);
                raw.gfx.push_back(g);
            }
        }
        gSPEndDisplayList(&g);
        raw.gfx.push_back(g);
    }

    // FNV-1a over the parts' resources, layers and transforms.
    static uint64_t HashParts(const std::vector<ModelPart>& parts) {
        uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](const void* data, size_t n) {
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < n; ++i) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
        };
        for (const auto& part : parts) {
            mix(part.resource.data(), part.resource.size());
            mix(&part.layer, sizeof(part.layer));
            mix(part.mtx, sizeof(part.mtx));
            mix(&part.billboard, sizeof(part.billboard));
            mix(part.anchor, sizeof(part.anchor));
        }
        // Light edits must re-render every framebuffer.
        const PreviewLighting& light = GetPreviewLighting();
        mix(&light.enabled, sizeof(light.enabled));
        mix(light.ambient, sizeof(light.ambient));
        mix(light.color, sizeof(light.color));
        mix(light.position, sizeof(light.position));
        mix(&light.intensity, sizeof(light.intensity));
        mix(&light.falloff, sizeof(light.falloff));
        return h;
    }

    // Reuse the slot owned by `name`, else one whose owner isn't visible, else
    // any free slot; -1 when exhausted.
    int AssignSlot(const std::string& name, const std::vector<bool>& used,
                   const std::unordered_set<std::string>& visible) {
        for (size_t i = 0; i < mFbPool.size(); ++i) {
            if (!used[i] && mFbPool[i].fbId >= 0 && mFbPool[i].owner == name) {
                return (int)i;
            }
        }
        for (size_t i = 0; i < mFbPool.size(); ++i) {
            if (!used[i] && (mFbPool[i].owner.empty() || visible.count(mFbPool[i].owner) == 0)) {
                return (int)i;
            }
        }
        for (size_t i = 0; i < mFbPool.size(); ++i) {
            if (!used[i]) {
                return (int)i;
            }
        }
        return -1;
    }

    void PumpAudio() {
        if (mAudioTotalFrames == 0) {
            return;
        }
        auto audio = Ship::Context::GetInstance()->GetAudio();
        auto player = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
        if (player == nullptr || !player->IsInitialized()) {
            return;
        }
        const int outCh = player->GetNumOutputChannels();
        const int need = player->GetDesiredBuffered() - player->Buffered();
        if (need <= 0) {
            return;
        }
        const int chunk = std::min(need, 4096);
        const double step = (double)mAudioSrcRate * mAudioSpeed / (double)player->GetSampleRate();
        std::vector<int16_t> out;
        out.reserve((size_t)chunk * outCh);
        for (int i = 0; i < chunk; ++i) {
            const size_t frame = (size_t)mAudioPos;
            if (frame >= mAudioTotalFrames) {
                break;
            }
            const int16_t l = (int16_t)(mAudioPcm[frame * mAudioSrcChannels] * mAudioVolume);
            const int16_t r = (int16_t)((mAudioSrcChannels > 1 ? mAudioPcm[frame * mAudioSrcChannels + 1]
                                                               : mAudioPcm[frame * mAudioSrcChannels]) *
                                        mAudioVolume);
            out.push_back(l);
            out.push_back(r);
            for (int c = 2; c < outCh; ++c) {
                out.push_back(0);
            }
            mAudioPos += step;
        }
        if (!out.empty()) {
            player->Play((const uint8_t*)out.data(), out.size() * sizeof(int16_t));
        }
        if ((size_t)mAudioPos >= mAudioTotalFrames) {
            StopAudio();
        }
    }

    // Assigns a framebuffer to every visible model and builds a command list
    // rendering the one whose content changed this frame. Returns nullptr if
    // nothing needs rendering.
    Gfx* BuildModelCommands(const std::shared_ptr<Fast::Fast3dWindow>& window) {
        mMtxReplacements.clear();
        if (mRenderList.empty()) {
            return nullptr;
        }
        auto rm = Ship::Context::GetInstance()->GetResourceManager();
        auto interp = window->GetInterpreterWeak().lock();
        if (rm == nullptr || interp == nullptr) {
            return nullptr;
        }
        auto* api = interp->GetCurrentRenderingAPI();
        if (api == nullptr) {
            return nullptr;
        }
        if ((int)mFbPool.size() != kFbPoolSize) {
            mFbPool.resize(kFbPoolSize);
        }

        std::unordered_set<std::string> visible;
        for (const auto& r : mRenderList) {
            visible.insert(r.name);
        }
        std::vector<bool> used(mFbPool.size(), false);

        // Phase 1: assign framebuffer slots and pick one dirty model to render.
        // The Metal backend only presents the first offscreen framebuffer drawn
        // per frame, so the rest keep their persisted render.
        int targetIdx = -1;
        int targetReqIdx = -1;
        std::vector<std::pair<int, int>> dirtySlots; // (pool idx, request idx)
        for (size_t ri = 0; ri < mRenderList.size(); ++ri) {
            const ModelRequest& req = mRenderList[ri];
            const int idx = AssignSlot(req.name, used, visible);
            if (idx < 0) {
                continue;
            }
            used[idx] = true;
            FbSlot& slot = mFbPool[idx];
            const bool newOwner = slot.owner != req.name;
            slot.owner = req.name;

            uint32_t w = std::max(1u, (uint32_t)req.size.x);
            uint32_t h = std::max(1u, (uint32_t)req.size.y);
            // Clamp to a sane max (preserving aspect): a giant rect would overflow
            // the int16 viewport scale and exceed GPU texture limits.
            constexpr uint32_t kMaxFb = 2048;
            if (w > kMaxFb || h > kMaxFb) {
                const float s = (float)kMaxFb / (float)std::max(w, h);
                w = std::max(1u, (uint32_t)(w * s));
                h = std::max(1u, (uint32_t)(h * s));
            }
            if (slot.fbId < 0) {
                slot.fbId = interp->CreateFrameBuffer(w, h, w, h, 0, false);
                slot.w = w;
                slot.h = h;
                slot.rendered = false;
            } else if (slot.w != w || slot.h != h) {
                api->UpdateFramebufferParameters(slot.fbId, w, h, 1, true, true, true, true);
                interp->mFrameBuffers[slot.fbId] = { w, h, w, h, w, h, false, false };
                slot.w = w;
                slot.h = h;
                slot.rendered = false; // resize discards the old contents
            }
            if (newOwner) {
                slot.rendered = false;
            }

            // Content hash catches pose changes (e.g. animation frames).
            const uint64_t partsHash = HashParts(req.parts);
            const bool dirty = !slot.rendered || !SameView(slot.lastView, req.view) || slot.lastPartsHash != partsHash;
            if (dirty) {
                dirtySlots.emplace_back(idx, (int)ri);
            }
        }

        // Round-robin so simultaneous animations don't starve each other.
        for (const auto& [idx, ri] : dirtySlots) {
            if (ri >= (int)mScanStart) {
                targetIdx = idx;
                targetReqIdx = ri;
                break;
            }
        }
        if (targetIdx < 0 && !dirtySlots.empty()) {
            targetIdx = dirtySlots.front().first;
            targetReqIdx = dirtySlots.front().second;
        }
        if (targetReqIdx >= 0) {
            mScanStart = (size_t)targetReqIdx + 1;
        }

        // Mirror the pool's owners so DrawModelParts can blit persisted slots.
        mNameToFb.clear();
        for (const auto& s : mFbPool) {
            if (s.fbId >= 0 && !s.owner.empty()) {
                mNameToFb[s.owner] = s.fbId;
            }
        }

        if (targetIdx < 0) {
            return nullptr; // nothing changed; every framebuffer keeps its last render
        }

        // Phase 2: render the target model into its framebuffer.
        const ModelRequest& req = mRenderList[targetReqIdx];
        FbSlot& slot = mFbPool[targetIdx];

        // Parts that fail to load are dropped rather than failing the model.
        std::vector<std::pair<const ModelPart*, Gfx*>> drawable;
        drawable.reserve(req.parts.size());
        for (const auto& part : req.parts) {
            Gfx* gfx = nullptr;
            auto rawIt = mRawModels.find(part.resource);
            if (rawIt != mRawModels.end()) {
                gfx = rawIt->second.gfx.data();
            } else {
                auto dl = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(part.resource));
                gfx = dl != nullptr ? dl->GetPointer() : nullptr;
            }
            if (gfx != nullptr) {
                drawable.emplace_back(&part, gfx);
            }
        }
        if (drawable.empty()) {
            slot.rendered = true; // nothing loadable; don't retry every frame
            slot.lastView = req.view;
            slot.lastPartsHash = HashParts(req.parts);
            return nullptr;
        }
        // Master-list order (layer 0..7) so transparency blends over opaque.
        std::stable_sort(drawable.begin(), drawable.end(),
                         [](const auto& a, const auto& b) { return a.first->layer < b.first->layer; });

        auto bit = mBoundsCache.find(req.name);
        if (bit == mBoundsCache.end()) {
            bit = mBoundsCache.emplace(req.name, ComputePartsBounds(req.parts, rm)).first;
        }
        const ModelBounds b = bit->second;

        const OrbitView& v = req.view;
        const float aspect = (float)slot.w / (float)slot.h;
        const float dist = (b.radius * 2.5f) / std::max(v.zoom, 0.02f);

        // Camera basis (matches LookAt). Pan shifts eye and target along x/y,
        // scaled by distance so drag speed is zoom-independent.
        const float zx = std::cos(v.pitch) * std::sin(v.yaw);
        const float zy = std::sin(v.pitch);
        const float zz = std::cos(v.pitch) * std::cos(v.yaw);
        float xx = zz, xz = -zx;
        const float xl = std::sqrt(xx * xx + xz * xz);
        if (xl > 0.0001f) {
            xx /= xl;
            xz /= xl;
        }
        const float yx = zy * xz;
        const float yy = zz * xx - zx * xz;
        const float yz = -zy * xx;
        const float panScale = dist * 0.0015f;
        const float cx = b.cx + (-v.panX * xx) * panScale + (v.panY * yx) * panScale;
        const float cy = b.cy + (v.panY * yy) * panScale;
        const float cz = b.cz + (-v.panX * xz) * panScale + (v.panY * yz) * panScale;

        const float eyeX = cx + dist * zx;
        const float eyeY = cy + dist * zy;
        const float eyeZ = cz + dist * zz;

        MtxF projF{};
        Perspective(projF, 45.0f, aspect, std::max(dist * 0.01f, 1.0f), dist * 4.0f + b.radius * 4.0f);
        MtxF viewF{};
        LookAt(viewF, eyeX, eyeY, eyeZ, cx, cy, cz);
        mMtxReplacements[&slot.proj] = projF;

        // Modelview per part = world * view. Billboards get an identity rotation
        // in view space at their anchor (mtxf_billboard). mPartMtxKeys provides
        // stable addresses for the replacement map; no reallocation before Run.
        mPartMtxKeys.resize(drawable.size());
        for (size_t i = 0; i < drawable.size(); ++i) {
            const ModelPart& part = *drawable[i].first;
            MtxF mv{};
            if (part.billboard) {
                MtxF bb{};
                bb.mf[0][0] = bb.mf[1][1] = bb.mf[2][2] = bb.mf[3][3] = 1.0f;
                for (int c = 0; c < 3; ++c) {
                    bb.mf[3][c] = part.anchor[0] * viewF.mf[0][c] + part.anchor[1] * viewF.mf[1][c] +
                                  part.anchor[2] * viewF.mf[2][c] + viewF.mf[3][c];
                }
                MulMtxF(mv, part.mtx, bb);
            } else {
                MulMtxF(mv, part.mtx, viewF);
            }
            mMtxReplacements[&mPartMtxKeys[i]] = mv;
        }

        const int16_t vx = (int16_t)(slot.w * 2);
        const int16_t vy = (int16_t)(slot.h * 2);
        slot.vp.vp.vscale[0] = vx; slot.vp.vp.vscale[1] = vy; slot.vp.vp.vscale[2] = G_MAXZ / 2;
        slot.vp.vp.vscale[3] = 0;
        slot.vp.vp.vtrans[0] = vx; slot.vp.vp.vtrans[1] = vy; slot.vp.vp.vtrans[2] = G_MAXZ / 2;
        slot.vp.vp.vtrans[3] = 0;

        // Point light approximated per part (the RSP only has directional
        // lights): direction from the light position to the part, color
        // attenuated by distance, supplied in view space. Parts that bind their
        // own material lights override it.
        const PreviewLighting& lighting = GetPreviewLighting();
        const auto to8 = [](float v) { return (uint8_t)std::clamp((int)(v * 255.0f + 0.5f), 0, 255); };
        const float lpx = b.cx + lighting.position[0] * b.radius;
        const float lpy = b.cy + lighting.position[1] * b.radius;
        const float lpz = b.cz + lighting.position[2] * b.radius;
        mPartLights.resize(drawable.size());
        for (size_t i = 0; i < drawable.size(); ++i) {
            const ModelPart& part = *drawable[i].first;
            const float px = part.billboard ? part.anchor[0] : part.mtx[3][0];
            const float py = part.billboard ? part.anchor[1] : part.mtx[3][1];
            const float pz = part.billboard ? part.anchor[2] : part.mtx[3][2];
            float dx = lpx - px, dy = lpy - py, dz = lpz - pz;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const float dlen = std::sqrt(distSq);
            if (dlen > 0.0001f) {
                dx /= dlen; dy /= dlen; dz /= dlen;
            } else {
                dy = 1.0f; dx = dz = 0.0f;
            }
            const float radii = b.radius > 0.0001f ? dlen / b.radius : 0.0f;
            const float atten = std::clamp(lighting.intensity / (1.0f + lighting.falloff * radii * radii), 0.0f, 1.0f);
            // World direction -> view space (rotation rows of the view matrix).
            const float vx = dx * viewF.mf[0][0] + dy * viewF.mf[1][0] + dz * viewF.mf[2][0];
            const float vy = dx * viewF.mf[0][1] + dy * viewF.mf[1][1] + dz * viewF.mf[2][1];
            const float vz = dx * viewF.mf[0][2] + dy * viewF.mf[1][2] + dz * viewF.mf[2][2];
            mPartLights[i] = gdSPDefLights1(
                to8(lighting.ambient[0]), to8(lighting.ambient[1]), to8(lighting.ambient[2]),
                to8(lighting.color[0] * atten), to8(lighting.color[1] * atten), to8(lighting.color[2] * atten),
                (int8_t)(vx * 127.0f), (int8_t)(vy * 127.0f), (int8_t)(vz * 127.0f));
        }

        mCmd.assign(48 + drawable.size() * 8, Gfx{});
        Gfx* p = mCmd.data();
        // Load f3d ucode + reset segment 0 (global state).
        p->words.w0 = ((uintptr_t)0xDD << 24) | ((uintptr_t)ucode_f3d & 0xFFFFFF);
        p->words.w1 = 0;
        ++p;
        __gSPSegment(p++, 0, 0x0);
        // Non-zero color image address so FILL rects clear color instead of
        // being treated as depth clears.
        gDPSetColorImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (void*)(uintptr_t)0x10);

        // Switch the render target to this model's framebuffer (G_SETFB 0x21).
        p->words.w0 = (uintptr_t)0x21 << 24;
        p->words.w1 = (uintptr_t)slot.fbId;
        ++p;
        // Color clear (depth was cleared by G_SETFB). The fill quad sits at the
        // near plane, so it must not write depth.
        gDPPipeSync(p++);
        gDPSetScissor(p++, G_SC_NON_INTERLACE, 0, 0, (int)slot.w, (int)slot.h);
        gDPSetRenderMode(p++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
        gDPSetCycleType(p++, G_CYC_FILL);
        gDPSetFillColor(p++, 0x10851085); // ~(18,18,22) packed RGBA5551 twice
        gDPFillRectangle(p++, 0, 0, (int)slot.w - 1, (int)slot.h - 1);
        gDPPipeSync(p++);

        gSPViewport(p++, &slot.vp);
        gSPClearGeometryMode(p++, 0xFFFFFFFF);
        gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetTexturePersp(p++, G_TP_PERSP);
        gDPSetTextureLOD(p++, G_TL_TILE);
        gDPSetTextureDetail(p++, G_TD_CLAMP);
        gDPSetTextureLUT(p++, G_TT_NONE);
        gDPSetTextureFilter(p++, G_TF_BILERP);
        gDPSetTextureConvert(p++, G_TC_FILT);
        gDPSetCombineKey(p++, G_CK_NONE);
        gDPSetAlphaCompare(p++, G_AC_NONE);
        gDPSetColorDither(p++, G_CD_DISABLE);
        gSPMatrix(p++, &slot.proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPSetCombineMode(p++, G_CC_MODULATEI, G_CC_MODULATEI_PRIM2);
        gDPSetDepthSource(p++, G_ZS_PIXEL);
        gDPSetCycleType(p++, G_CYC_1CYCLE);
        gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | (lighting.enabled ? G_LIGHTING : 0));
        // Per-layer render modes (SM64 renderModeTable_1Cycle, z-buffered).
        static const uint32_t kLayerCycle1[8] = {
            G_RM_ZB_OPA_SURF,     G_RM_AA_ZB_OPA_SURF,  G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_INTER,
            G_RM_AA_ZB_TEX_EDGE,  G_RM_AA_ZB_XLU_SURF,  G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_INTER,
        };
        static const uint32_t kLayerCycle2[8] = {
            G_RM_ZB_OPA_SURF2,    G_RM_AA_ZB_OPA_SURF2, G_RM_AA_ZB_OPA_DECAL2, G_RM_AA_ZB_OPA_INTER2,
            G_RM_AA_ZB_TEX_EDGE2, G_RM_AA_ZB_XLU_SURF2, G_RM_AA_ZB_XLU_DECAL2, G_RM_AA_ZB_XLU_INTER2,
        };
        int lastLayer = -1;
        for (size_t i = 0; i < drawable.size(); ++i) {
            const int layer = drawable[i].first->layer & 7;
            if (layer != lastLayer) {
                gDPPipeSync(p++);
                gDPSetRenderMode(p++, kLayerCycle1[layer], kLayerCycle2[layer]);
                lastLayer = layer;
            }
            gSPSetLights1(p++, mPartLights[i]);
            gSPMatrix(p++, &mPartMtxKeys[i], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
            __gSPDisplayList(p++, drawable[i].second);
        }
        gDPFullSync(p++);
        // Restore the main framebuffer (G_RESETFB 0x22) before the gui draws.
        p->words.w0 = (uintptr_t)0x22 << 24;
        p->words.w1 = 0;
        ++p;
        gSPEndDisplayList(p++);

        slot.rendered = true;
        slot.lastView = req.view;
        slot.lastPartsHash = HashParts(req.parts);
        return mCmd.data();
    }

    static constexpr int kFbPoolSize = 24;

    // A model preview render request, queued by DrawModelParts during the gui draw.
    struct ModelRequest {
        std::string name;
        std::vector<ModelPart> parts;
        ImVec2 topLeft;
        ImVec2 size;
        OrbitView view;
    };

    std::vector<ModelRequest> mRequests;   // filled this frame, rendered next
    std::vector<ModelRequest> mRenderList; // snapshot being rendered this frame
    std::unordered_map<std::string, int> mNameToFb;          // model -> framebuffer id
    std::unordered_map<std::string, ModelBounds> mBoundsCache;
    std::vector<FbSlot> mFbPool;
    size_t mScanStart = 0; // round-robin cursor for the one-render-per-frame pick
    // Stable keys for the Mtx->MtxF replacement map.
    std::vector<Mtx> mPartMtxKeys;
    // Per-part light evaluation; alive through the interpreter Run.
    std::vector<Lights1> mPartLights;
    std::unordered_map<std::string, RawModel> mRawModels;
    // One-shot sample playback, pushed to the LUS audio player each frame with
    // nearest-neighbor resampling to the device rate.
    std::vector<int16_t> mAudioPcm;
    size_t mAudioTotalFrames = 0;
    double mAudioPos = 0.0;
    int mAudioSrcRate = 0;
    int mAudioSrcChannels = 1;
    float mAudioVolume = 1.0f;
    float mAudioSpeed = 1.0f;
    std::vector<Gfx> mCmd;
    std::unordered_map<Mtx*, MtxF> mMtxReplacements;
};

} // namespace

std::unique_ptr<BaseBackend> CreateLusBackend() {
    return std::make_unique<LusBackend>();
}

} // namespace UI

#endif // BUILD_UI
