#ifdef BUILD_UI

#include "ui/backends/LusBackend.h"

#include <filesystem>
#include <vector>

#include "Companion.h"
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

    void SetPreviewBackdrop(const std::string& key, const uint8_t* rgba, int width, int height) override {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            mBackdrops.erase(key);
            return;
        }
        // Texel buffers are content-keyed and never freed: the interpreter's
        // texture cache keys on the data address, so reusing one buffer for a
        // different image would serve the stale GPU texture.
        const uint64_t hash = HashBytes(rgba, (size_t)width * height * 4, 1469598103934665603ull + width * 131 + height);
        auto& texels = mBackdropTexelCache[hash];
        if (texels.empty()) {
            // N64 RGBA16 texels (big-endian 5551) for the texture-rectangle strips.
            texels.resize((size_t)width * height * 2);
            for (int i = 0; i < width * height; ++i) {
                const uint16_t v = (uint16_t)(((rgba[i * 4 + 0] >> 3) << 11) | ((rgba[i * 4 + 1] >> 3) << 6) |
                                              ((rgba[i * 4 + 2] >> 3) << 1) | 1);
                texels[i * 2 + 0] = (uint8_t)(v >> 8);
                texels[i * 2 + 1] = (uint8_t)(v & 0xFF);
            }
        }
        Backdrop& bd = mBackdrops[key];
        bd.texels = &texels;
        bd.w = width;
        bd.h = height;
        bd.generation = hash;
    }

    bool RegisterGameDList(const std::string& name, const UI::GfxBundle& bundle, uint32_t entryOffset) override {
        if (mGameGfx.count(name) != 0) {
            return true;
        }
        if (bundle.blob == nullptr || bundle.blob->empty()) {
            return false;
        }
        auto bit = mGameBundles.find(bundle.blob.get());
        if (bit == mGameBundles.end()) {
            auto gb = std::make_shared<GameBundle>();
            gb->blob = bundle.blob;
            gb->vtxBase = bundle.vtxBase;
            gb->vtxSize = bundle.vtxSize;
            // Two passes: allocate every dlist first so G_DL can cross-reference.
            for (const auto& dl : bundle.dlists) {
                gb->dlists[dl.offset].resize(dl.words.size() / 2);
            }
            uint8_t* blobData = gb->blob->data();
            const size_t blobSize = gb->blob->size();
            for (const auto& dl : bundle.dlists) {
                auto& out = gb->dlists[dl.offset];
                for (size_t i = 0; i + 1 < dl.words.size(); i += 2) {
                    const uint32_t w0 = dl.words[i];
                    const uint32_t w1 = dl.words[i + 1];
                    const uint8_t op = w0 >> 24;
                    Gfx& g = out[i / 2];
                    g.words.w0 = w0;
                    g.words.w1 = w1;
                    const auto noop = [&g] {
                        g.words.w0 = 0;
                        g.words.w1 = 0;
                    };
                    if (op == 0x01) { // G_VTX: vtxBase-relative byte offset
                        const uint32_t numv = (w0 >> 12) & 0xFF;
                        const uint64_t off = (uint64_t)gb->vtxBase + w1;
                        if (numv != 0 && off + (uint64_t)numv * 16 <= blobSize) {
                            g.words.w1 = (uintptr_t)(blobData + off);
                            gb->meta[dl.offset].vtxSpans.emplace_back((uint32_t)off, numv);
                        } else {
                            noop();
                        }
                    } else if (op == 0xDE) { // G_DL: blob offset of another dlist
                        auto tit = gb->dlists.find(w1);
                        if (tit != gb->dlists.end()) {
                            g.words.w1 = (uintptr_t)tit->second.data();
                            gb->meta[dl.offset].children.push_back(w1);
                        } else {
                            g.words.w0 = (uintptr_t)0xDF << 24; // missing target: end
                            g.words.w1 = 0;
                        }
                    } else if (op == 0xFD) { // G_SETTIMG
                        if (w1 < blobSize) {
                            g.words.w1 = (uintptr_t)(blobData + w1);
                        } else {
                            noop();
                        }
                    } else if (op == 0x04 || op == 0xDA || op == 0xDC) {
                        // BRANCH_Z / G_MTX / G_MOVEMEM carry addresses we can't
                        // resolve; drop them rather than dereference garbage.
                        noop();
                    }
                }
            }
            bit = mGameBundles.emplace(bundle.blob.get(), std::move(gb)).first;
        }
        auto& gb = bit->second;
        auto eit = gb->dlists.find(entryOffset);
        if (eit == gb->dlists.end() || eit->second.empty()) {
            return false;
        }
        // Bounds from the vertices this dlist (and its G_DL children) actually
        // loads, so the camera frames each part instead of the whole table.
        if (mBoundsCache.find(name) == mBoundsCache.end()) {
            float mn[3] = { 1e18f, 1e18f, 1e18f }, mx[3] = { -1e18f, -1e18f, -1e18f };
            bool any = false;
            std::vector<uint32_t> queue{ entryOffset };
            std::unordered_set<uint32_t> seen{ entryOffset };
            while (!queue.empty()) {
                const uint32_t off = queue.back();
                queue.pop_back();
                const auto mit = gb->meta.find(off);
                if (mit == gb->meta.end()) {
                    continue;
                }
                for (const auto& [vtxOff, numv] : mit->second.vtxSpans) {
                    for (uint32_t i = 0; i < numv; ++i) {
                        const int16_t* pos = (const int16_t*)(gb->blob->data() + vtxOff + i * 16);
                        for (int a = 0; a < 3; ++a) {
                            mn[a] = std::min(mn[a], (float)pos[a]);
                            mx[a] = std::max(mx[a], (float)pos[a]);
                        }
                        any = true;
                    }
                }
                for (const uint32_t child : mit->second.children) {
                    if (seen.insert(child).second) {
                        queue.push_back(child);
                    }
                }
            }
            // Fallback: the blob's whole vertex table.
            if (!any && gb->vtxSize >= 16) {
                const uint8_t* base = gb->blob->data() + gb->vtxBase;
                const size_t count = std::min<size_t>(gb->vtxSize / 16, (gb->blob->size() - gb->vtxBase) / 16);
                for (size_t i = 0; i < count; ++i) {
                    const int16_t* pos = (const int16_t*)(base + i * 16);
                    for (int a = 0; a < 3; ++a) {
                        mn[a] = std::min(mn[a], (float)pos[a]);
                        mx[a] = std::max(mx[a], (float)pos[a]);
                    }
                    any = true;
                }
            }
            if (any) {
                ModelBounds b;
                b.cx = (mn[0] + mx[0]) * 0.5f;
                b.cy = (mn[1] + mx[1]) * 0.5f;
                b.cz = (mn[2] + mx[2]) * 0.5f;
                const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
                b.radius = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1.0f);
                mBoundsCache.emplace(name, b);
            }
        }
        mGameGfx[name] = { gb, eit->second.data() };
        return true;
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

    // Union of each part's bounds transformed by its matrix. Parts far larger
    // than the rest (sky domes, backdrops) are excluded from framing so the
    // camera isn't pushed inside them.
    ModelBounds ComputePartsBounds(const std::vector<ModelPart>& parts,
                                   const std::shared_ptr<Ship::ResourceManager>& rm) {
        struct Sphere {
            float c[3];
            float r;
        };
        std::vector<Sphere> spheres;
        spheres.reserve(parts.size());
        for (const auto& part : parts) {
            auto bit = mBoundsCache.find(part.resource);
            if (bit == mBoundsCache.end() && mGameGfx.count(part.resource) == 0) {
                auto dl = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(part.resource));
                if (dl == nullptr) {
                    continue;
                }
                bit = mBoundsCache.emplace(part.resource, ComputeBounds(dl, rm)).first;
            }
            if (bit == mBoundsCache.end()) {
                continue;
            }
            const ModelBounds& pb = bit->second;
            const auto& m = part.mtx;
            // Billboard matrices are anchor-relative; shift by the anchor.
            const float ax = part.billboard ? part.anchor[0] : 0.0f;
            const float ay = part.billboard ? part.anchor[1] : 0.0f;
            const float az = part.billboard ? part.anchor[2] : 0.0f;
            Sphere s;
            s.c[0] = pb.cx * m[0][0] + pb.cy * m[1][0] + pb.cz * m[2][0] + m[3][0] + ax;
            s.c[1] = pb.cx * m[0][1] + pb.cy * m[1][1] + pb.cz * m[2][1] + m[3][1] + ay;
            s.c[2] = pb.cx * m[0][2] + pb.cy * m[1][2] + pb.cz * m[2][2] + m[3][2] + az;
            float scale = 0.0f;
            for (int r = 0; r < 3; ++r) {
                scale = std::max(scale, std::sqrt(m[r][0] * m[r][0] + m[r][1] * m[r][1] + m[r][2] * m[r][2]));
            }
            s.r = pb.radius * std::max(scale, 0.001f);
            spheres.push_back(s);
        }

        float cutoff = 1e18f;
        if (spheres.size() >= 4) {
            std::vector<float> radii;
            radii.reserve(spheres.size());
            for (const auto& s : spheres) {
                radii.push_back(s.r);
            }
            std::nth_element(radii.begin(), radii.begin() + radii.size() / 2, radii.end());
            cutoff = std::max(radii[radii.size() / 2] * 6.0f, 1.0f);
        }

        float mn[3] = { 1e18f, 1e18f, 1e18f };
        float mx[3] = { -1e18f, -1e18f, -1e18f };
        bool any = false;
        for (const auto& s : spheres) {
            if (s.r > cutoff) {
                continue;
            }
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], s.c[a] - s.r);
                mx[a] = std::max(mx[a], s.c[a] + s.r);
            }
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
            h = h * 1099511628211ULL + (part.texture != nullptr ? part.texture->rasterOffset + 1 : 0);
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
        const PreviewAtmosphere& atmo = GetPreviewAtmosphere();
        mix(&atmo.fogEnabled, sizeof(atmo.fogEnabled));
        mix(atmo.fogColor, sizeof(atmo.fogColor));
        mix(&atmo.fogStart, sizeof(atmo.fogStart));
        mix(&atmo.fogEnd, sizeof(atmo.fogEnd));
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

    // Emits the tile setup for a part texture using the game's conventions
    // (texel wrap masks from dimensions, CI palettes via TLUT).
    static int ILog2(uint16_t v) {
        int m = 0;
        while ((1 << (m + 1)) <= v) {
            m++;
        }
        return m;
    }

    Gfx* EmitPartTexture(Gfx* p, const UI::PartTexture& t, bool fog) {
        uint8_t* raster = t.blob->data() + t.rasterOffset;
        const int maskS = ILog2(t.width);
        const int maskT = ILog2(t.height);
        const bool aux = t.auxMode != 0 && t.auxRasterOffset != 0 && t.auxWidth != 0 && t.auxHeight != 0;
        gDPPipeSync(p++);
        gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        // pmret SolidCombineModes, TINT_COMBINE_NONE / TINT_COMBINE_FOG
        // columns (fog moves the second cycle to PASS so blender cycle 1 can
        // fog the combined color).
        if (aux) {
            if (fog) { // G_CC_INTERFERENCE, PM_CC2_MULTIPLY_SHADE
                gDPSetCombineLERP(p++, TEXEL0, 0, TEXEL1, 0, TEXEL0, 0, TEXEL1, 0,
                                  COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);
            } else { // PM_CC_ALT_INTERFERENCE, G_CC_MODULATEIA2
                gDPSetCombineLERP(p++, TEXEL1, 0, TEXEL0, 0, TEXEL1, 0, TEXEL0, 0,
                                  COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
            }
        } else if (t.combine == 1) {
            if (fog) {
                gDPSetCombineMode(p++, G_CC_BLENDRGBA, G_CC_PASS2);
            } else {
                gDPSetCombineMode(p++, G_CC_BLENDRGBA, G_CC_BLENDRGBA);
            }
        } else if (t.combine == 2) {
            if (fog) {
                gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_PASS2);
            } else {
                gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
            }
        } else {
            if (fog) {
                gDPSetCombineMode(p++, G_CC_MODULATEIDECALA, G_CC_PASS2);
            } else {
                gDPSetCombineMode(p++, G_CC_MODULATEIA, G_CC_MODULATEIA);
            }
        }
        const bool auxCi = aux && t.auxFmt == G_IM_FMT_CI;
        if (t.fmt == G_IM_FMT_CI || auxCi) {
            gDPSetTextureLUT(p++, G_TT_RGBA16);
            if (t.fmt == G_IM_FMT_CI) {
                uint8_t* pal = t.blob->data() + t.paletteOffset;
                if (t.siz == G_IM_SIZ_8b) {
                    gDPLoadTLUT_pal256(p++, pal);
                } else {
                    gDPLoadTLUT_pal16(p++, 0, pal);
                }
            }
            // Independent aux palette rides in slot 1 (shared-raster aux reuses
            // the main palette).
            if (auxCi && t.auxMode == 3 && t.auxPaletteOffset != 0 && t.auxSiz != G_IM_SIZ_8b) {
                gDPLoadTLUT_pal16(p++, 1, t.blob->data() + t.auxPaletteOffset);
            }
        } else {
            gDPSetTextureLUT(p++, G_TT_NONE);
        }
        switch (t.siz) {
            case G_IM_SIZ_4b:
                gDPLoadTextureBlock_4b(p++, raster, t.fmt, t.width, t.height, 0, t.cmS, t.cmT, maskS, maskT,
                                       G_TX_NOLOD, G_TX_NOLOD);
                break;
            case G_IM_SIZ_8b:
                gDPLoadTextureBlock(p++, raster, t.fmt, G_IM_SIZ_8b, t.width, t.height, 0, t.cmS, t.cmT, maskS, maskT,
                                    G_TX_NOLOD, G_TX_NOLOD);
                break;
            case G_IM_SIZ_32b:
                gDPLoadTextureBlock(p++, raster, t.fmt, G_IM_SIZ_32b, t.width, t.height, 0, t.cmS, t.cmT, maskS, maskT,
                                    G_TX_NOLOD, G_TX_NOLOD);
                break;
            default:
                gDPLoadTextureBlock(p++, raster, t.fmt, G_IM_SIZ_16b, t.width, t.height, 0, t.cmS, t.cmT, maskS, maskT,
                                    G_TX_NOLOD, G_TX_NOLOD);
                break;
        }
        if (aux) {
            // Second tile into the upper TMEM slot (any nonzero tmem selects
            // the interpreter's slot 1); TEXEL1 blends it in cycle 1.
            uint8_t* auxRaster = t.blob->data() + t.auxRasterOffset;
            const uint32_t mainBytes = ((uint32_t)t.width * t.height * (4u << t.siz)) / 8;
            const uint32_t tmem = std::max(1u, (mainBytes + 7) >> 3);
            const int auxPalIdx = t.auxMode == 3 && auxCi && t.auxSiz != G_IM_SIZ_8b ? 1 : 0;
            const int auxMaskS = ILog2(t.auxWidth);
            const int auxMaskT = ILog2(t.auxHeight);
            switch (t.auxSiz) {
                case G_IM_SIZ_4b:
                    gDPLoadMultiBlock_4b(p++, auxRaster, tmem, 1, t.auxFmt, t.auxWidth, t.auxHeight, auxPalIdx,
                                         t.auxCmS, t.auxCmT, auxMaskS, auxMaskT, G_TX_NOLOD, G_TX_NOLOD);
                    break;
                case G_IM_SIZ_8b:
                    gDPLoadMultiBlock(p++, auxRaster, tmem, 1, t.auxFmt, G_IM_SIZ_8b, t.auxWidth, t.auxHeight,
                                      auxPalIdx, t.auxCmS, t.auxCmT, auxMaskS, auxMaskT, G_TX_NOLOD, G_TX_NOLOD);
                    break;
                case G_IM_SIZ_32b:
                    gDPLoadMultiBlock(p++, auxRaster, tmem, 1, t.auxFmt, G_IM_SIZ_32b, t.auxWidth, t.auxHeight,
                                      auxPalIdx, t.auxCmS, t.auxCmT, auxMaskS, auxMaskT, G_TX_NOLOD, G_TX_NOLOD);
                    break;
                default:
                    gDPLoadMultiBlock(p++, auxRaster, tmem, 1, t.auxFmt, G_IM_SIZ_16b, t.auxWidth, t.auxHeight,
                                      auxPalIdx, t.auxCmS, t.auxCmT, auxMaskS, auxMaskT, G_TX_NOLOD, G_TX_NOLOD);
                    break;
            }
        }
        return p;
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
            const uint64_t partsHash = HashRequest(req);
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
        if (std::getenv("TORCH_UI_RENDERLOG") != nullptr) {
            static int sFrame = 0;
            fprintf(stderr, "[render] f%d reqs=%zu dirty=%zu target=%s\n", sFrame++, mRenderList.size(),
                    dirtySlots.size(), targetReqIdx >= 0 ? mRenderList[targetReqIdx].name.c_str() : "(none)");
        }

        // Mirror the pool's owners so DrawModelParts can blit persisted slots.
        // Unrendered slots are excluded: a freshly created/resized Metal
        // texture holds stale VRAM (other previews' old renders) until this
        // slot's round-robin turn comes; blitting it bleeds display lists.
        mNameToFb.clear();
        for (const auto& s : mFbPool) {
            if (s.fbId >= 0 && !s.owner.empty() && s.rendered) {
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
        struct DrawEntry {
            const ModelPart* first;
            Gfx* second;
            bool raw;
        };
        std::vector<DrawEntry> drawable;
        drawable.reserve(req.parts.size());
        for (const auto& part : req.parts) {
            Gfx* gfx = nullptr;
            bool raw = false;
            auto rawIt = mRawModels.find(part.resource);
            auto gameIt = mGameGfx.find(part.resource);
            if (rawIt != mRawModels.end()) {
                gfx = rawIt->second.gfx.data();
                raw = true;
            } else if (gameIt != mGameGfx.end()) {
                gfx = gameIt->second.second;
            } else {
                auto dl = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(part.resource));
                gfx = dl != nullptr ? dl->GetPointer() : nullptr;
            }
            if (gfx != nullptr) {
                drawable.push_back({ &part, gfx, raw });
            }
        }
        if (drawable.empty()) {
            slot.rendered = true; // nothing loadable; don't retry every frame
            slot.lastView = req.view;
            slot.lastPartsHash = HashRequest(req);
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
        Perspective(projF, 45.0f, aspect, std::max(dist * 0.08f, 1.0f), dist * 4.0f + b.radius * 4.0f);
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

        const Backdrop* backdrop = nullptr;
        if (const auto bdit = mBackdrops.find(req.name); bdit != mBackdrops.end() && bdit->second.w > 0) {
            backdrop = &bdit->second;
        }
        size_t backdropCmds = 0;
        if (backdrop != nullptr) {
            const int rowsPer = std::max(1, 2048 / backdrop->w);
            backdropCmds = 32 + (size_t)((backdrop->h + rowsPer - 1) / rowsPer) * 12;
        }
        mCmd.assign(80 + drawable.size() * 64 + backdropCmds, Gfx{});
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
        gDPSetCombineMode(p++, G_CC_SHADE, G_CC_SHADE);
        gDPSetDepthSource(p++, G_ZS_PIXEL);
        gDPSetCycleType(p++, G_CYC_1CYCLE);
        if (backdrop != nullptr) {
            // Cover-fit texture-rectangle strips (TMEM holds 2048 RGBA16
            // texels per load), drawn before any geometry, no depth writes.
            gDPPipeSync(p++);
            gDPSetTexturePersp(p++, G_TP_NONE);
            gDPSetRenderMode(p++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
            gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
            gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
            const float sc = std::max((float)slot.w / backdrop->w, (float)slot.h / backdrop->h);
            const float offX = ((float)slot.w - backdrop->w * sc) * 0.5f;
            const float offY = ((float)slot.h - backdrop->h * sc) * 0.5f;
            const int rowsPer = std::max(1, 2048 / backdrop->w);
            const int dsdx = (int)(1024.0f / sc); // s5.10 texels per pixel
            for (int row = 0; row < backdrop->h; row += rowsPer) {
                const int rows = std::min(rowsPer, backdrop->h - row);
                gDPLoadTextureBlock(p++, backdrop->texels->data() + (size_t)row * backdrop->w * 2, G_IM_FMT_RGBA,
                                    G_IM_SIZ_16b, backdrop->w, rows, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK,
                                    G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                const float y0f = offY + row * sc;
                const int x0 = std::max((int)offX, 0);
                const int x1 = std::min((int)std::ceil(offX + backdrop->w * sc), (int)slot.w);
                const int y0 = std::clamp((int)y0f, 0, (int)slot.h);
                const int y1 = std::clamp((int)std::ceil(offY + (row + rows) * sc), 0, (int)slot.h);
                if (x1 <= x0 || y1 <= y0) {
                    continue;
                }
                const int S = (int)((x0 - offX) / sc * 32.0f);
                const int T = (int)((y0 - y0f) / sc * 32.0f);
                gSPTextureRectangle(p++, x0 << 2, y0 << 2, x1 << 2, y1 << 2, G_TX_RENDERTILE, S, T, dsdx, dsdx);
            }
            gDPPipeSync(p++);
            gDPSetTexturePersp(p++, G_TP_PERSP);
            gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
            gDPSetCombineMode(p++, G_CC_SHADE, G_CC_SHADE);
        }
        const PreviewAtmosphere& atmo = GetPreviewAtmosphere();
        const bool fog = atmo.fogEnabled;
        uint32_t fogWord = 0;
        if (fog) {
            gDPSetFogColor(p++, to8(atmo.fogColor[0]), to8(atmo.fogColor[1]), to8(atmo.fogColor[2]), 255);
            // G_MOVEWORD/G_MW_FOG, raw-encoded per dialect (the compiled gbi
            // macros target another one). w1 = fog scale << 16 | fog offset.
            const int range = std::max(atmo.fogEnd - atmo.fogStart, 1);
            const uint16_t fm = (uint16_t)(int16_t)(128000 / range);
            const uint16_t fo = (uint16_t)(int16_t)(((500 - atmo.fogStart) * 256) / range);
            fogWord = ((uint32_t)fm << 16) | fo;
            p->words.w0 = ((uintptr_t)0xBC << 24) | 0x08;
            p->words.w1 = fogWord;
            ++p;
        }
        const uint32_t baseGeo = G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | (fog ? G_FOG : 0);
        gSPSetGeometryMode(p++, baseGeo | (lighting.enabled ? G_LIGHTING : 0));
        // Per-layer render modes (SM64 renderModeTable_1Cycle, z-buffered).
        static const uint32_t kLayerCycle1[8] = {
            G_RM_ZB_OPA_SURF,     G_RM_AA_ZB_OPA_SURF,  G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_INTER,
            G_RM_AA_ZB_TEX_EDGE,  G_RM_AA_ZB_XLU_SURF,  G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_INTER,
        };
        static const uint32_t kLayerCycle2[8] = {
            G_RM_ZB_OPA_SURF2,    G_RM_AA_ZB_OPA_SURF2, G_RM_AA_ZB_OPA_DECAL2, G_RM_AA_ZB_OPA_INTER2,
            G_RM_AA_ZB_TEX_EDGE2, G_RM_AA_ZB_XLU_SURF2, G_RM_AA_ZB_XLU_DECAL2, G_RM_AA_ZB_XLU_INTER2,
        };
        uint32_t lastRm1 = 0, lastRm2 = 0;
        int lastCyc = -1;
        for (size_t i = 0; i < drawable.size(); ++i) {
            const int layer = drawable[i].first->layer & 7;
            uint32_t rm1, rm2;
            int cyc;
            if (drawable[i].first->renderMode1 != 0 || drawable[i].first->renderMode2 != 0) {
                rm1 = drawable[i].first->renderMode1;
                rm2 = drawable[i].first->renderMode2;
                cyc = drawable[i].first->cycleType;
            } else {
                rm1 = kLayerCycle1[layer];
                rm2 = kLayerCycle2[layer];
                cyc = 1;
            }
            if (fog) {
                // Fog runs in blender cycle 1 (the game's RENDER_CLASS_FOG).
                rm1 = G_RM_FOG_SHADE_A;
                cyc = 2;
            }
            if (rm1 != lastRm1 || rm2 != lastRm2 || cyc != lastCyc) {
                gDPPipeSync(p++);
                gDPSetCycleType(p++, cyc == 2 ? G_CYC_2CYCLE : G_CYC_1CYCLE);
                gDPSetRenderMode(p++, rm1, rm2);
                lastRm1 = rm1;
                lastRm2 = rm2;
                lastCyc = cyc;
            }
            gSPSetLights1(p++, mPartLights[i]);
            gSPMatrix(p++, &mPartMtxKeys[i], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
            const ModelPart& partRef = *drawable[i].first;
            if (partRef.texture != nullptr && partRef.texture->blob != nullptr) {
                p = EmitPartTexture(p, *partRef.texture, fog);
            } else {
                gDPPipeSync(p++);
                gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
                gDPSetTextureLUT(p++, G_TT_NONE);
                // Shade-only: MODULATE variants still sample TEXEL0, which
                // would multiply in whatever texture the previous part bound.
                if (fog) {
                    gDPSetCombineMode(p++, G_CC_SHADE, G_CC_PASS2);
                } else {
                    gDPSetCombineMode(p++, G_CC_SHADE, G_CC_SHADE);
                }
            }
            const UcodeHandlers gameUcode = ConfigUcode();
            if (!drawable[i].raw && gameUcode != ucode_f3d) {
                // Run the game's list under its own dialect, then return to
                // F3D for the prefix commands. G_DL is 0xDE on f3dex2.
                p->words.w0 = ((uintptr_t)0xDD << 24) | ((uintptr_t)gameUcode & 0xFFFFFF);
                p->words.w1 = 0;
                ++p;
                if (fog) {
                    // Ucode loads reset the RSP fog factor; re-arm it in the
                    // game dialect (f3dex2 moveword: index in w0 bits 16-23).
                    if (gameUcode == ucode_f3dex2) {
                        p->words.w0 = ((uintptr_t)0xDB << 24) | ((uintptr_t)0x08 << 16);
                    } else {
                        p->words.w0 = ((uintptr_t)0xBC << 24) | 0x08;
                    }
                    p->words.w1 = fogWord;
                    ++p;
                }
                if (gameUcode == ucode_f3dex2) {
                    // Geometry-mode bits are stored raw and read per-dialect:
                    // F3D's SMOOTH (0x200) is f3dex2's CULL_FRONT. Re-set the
                    // baseline with f3dex2 values (SMOOTH = 0x200000).
                    p->words.w0 = (uintptr_t)0xD9 << 24; // clear all
                    p->words.w1 = 0x1 | 0x4 | 0x200000 | (fog ? 0x10000 : 0) |
                                  (lighting.enabled && !partRef.unlit ? 0x20000 : 0);
                    ++p;
                }
                p->words.w0 = (uintptr_t)(gameUcode == ucode_f3dex2 ? 0xDE : 0x06) << 24;
                p->words.w1 = (uintptr_t)drawable[i].second;
                ++p;
                p->words.w0 = ((uintptr_t)0xDD << 24) | ((uintptr_t)ucode_f3d & 0xFFFFFF);
                p->words.w1 = 0;
                ++p;
                if (fog) {
                    p->words.w0 = ((uintptr_t)0xBC << 24) | 0x08;
                    p->words.w1 = fogWord;
                    ++p;
                }
                if (gameUcode == ucode_f3dex2) {
                    gSPClearGeometryMode(p++, 0xFFFFFFFF);
                    gSPSetGeometryMode(p++, baseGeo | (lighting.enabled ? G_LIGHTING : 0));
                }
            } else {
                __gSPDisplayList(p++, drawable[i].second);
            }
        }
        gDPFullSync(p++);
        // Restore the main framebuffer (G_RESETFB 0x22) before the gui draws.
        p->words.w0 = (uintptr_t)0x22 << 24;
        p->words.w1 = 0;
        ++p;
        gSPEndDisplayList(p++);
        if ((size_t)(p - mCmd.data()) > mCmd.size()) {
            SPDLOG_ERROR("Preview command list overflow: {} > {}", (size_t)(p - mCmd.data()), mCmd.size());
        }

        if (const char* dump = std::getenv("TORCH_UI_DUMPCMDS");
            dump != nullptr && req.name.find(dump) != std::string::npos) {
            static int sRenderCount = 0;
            size_t texCount = 0;
            for (const auto& d : drawable) {
                texCount += d.first->texture != nullptr ? 1 : 0;
            }
            fprintf(stderr, "[cmds] render %d of %s: %zu cmds, %zu/%zu parts textured in request\n", sRenderCount++,
                    req.name.c_str(), (size_t)(p - mCmd.data()), texCount, drawable.size());
            for (Gfx* g = mCmd.data(); g < p; ++g) {
                fprintf(stderr, "[cmds]   %02X %08X %016llX\n", (unsigned)(g->words.w0 >> 24) & 0xFF,
                        (unsigned)(g->words.w0 & 0xFFFFFF), (unsigned long long)g->words.w1);
            }
        }

        slot.rendered = true;
        slot.lastView = req.view;
        slot.lastPartsHash = HashRequest(req);
        return mCmd.data();
    }

    // The game's display lists use the configured GBI dialect; our generated
    // preview meshes are always F3D.
    static UcodeHandlers ConfigUcode() {
        if (Companion::Instance == nullptr) {
            return ucode_f3d;
        }
        switch (Companion::Instance->GetGBIVersion()) {
            case GBIVersion::f3db:
                return ucode_f3db;
            case GBIVersion::f3dex:
                return ucode_f3dex;
            case GBIVersion::f3dexb:
                return ucode_f3dexb;
            case GBIVersion::f3dex2:
                return ucode_f3dex2;
            default:
                return ucode_f3d;
        }
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

    // Parts hash plus the backdrop generation, so backdrop edits re-render.
    uint64_t HashRequest(const ModelRequest& req) {
        uint64_t h = HashParts(req.parts);
        const auto it = mBackdrops.find(req.name);
        if (it != mBackdrops.end()) {
            h = h * 1099511628211ull + it->second.generation;
        }
        return h;
    }

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
    // Game-dialect display lists registered from parsed blobs. Pointer
    // operands are resolved once into owned native Gfx arrays.
    struct GameBundle {
        std::shared_ptr<std::vector<uint8_t>> blob;
        uint32_t vtxBase = 0;
        uint32_t vtxSize = 0;
        std::unordered_map<uint32_t, std::vector<Gfx>> dlists;
        // Vertex spans (blob offset, count) and G_DL children per dlist, for
        // per-dlist bounds.
        struct DlMeta {
            std::vector<std::pair<uint32_t, uint32_t>> vtxSpans;
            std::vector<uint32_t> children;
        };
        std::unordered_map<uint32_t, DlMeta> meta;
    };
    std::unordered_map<void*, std::shared_ptr<GameBundle>> mGameBundles;   // keyed by blob
    std::unordered_map<std::string, std::pair<std::shared_ptr<GameBundle>, Gfx*>> mGameGfx;

    // Screen-space backdrops (N64 RGBA16 texels) drawn as texture-rectangle
    // strips before a model's parts.
    struct Backdrop {
        const std::vector<uint8_t>* texels = nullptr;
        int w = 0, h = 0;
        uint64_t generation = 0;
    };
    std::unordered_map<std::string, Backdrop> mBackdrops;
    // Content-hash -> converted texels; entries outlive backdrop switches.
    std::unordered_map<uint64_t, std::vector<uint8_t>> mBackdropTexelCache;

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
