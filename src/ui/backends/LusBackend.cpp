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
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/File.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <libultraship/libultra/gbi.h>

#include <cmath>
#include <unordered_map>
#include <unordered_set>

// The only translation unit that touches libultraship.
namespace UI {

namespace {

// Hosts the Torch viewer inside LUS's gui draw loop. LUS calls Draw() on every
// registered GuiWindow each frame between ImGui::NewFrame and ImGui::Render; we
// override it to drive the active View directly (the View owns its own
// fullscreen ImGui window, so we bypass GuiWindow's default Begin/End).
class ViewHostWindow : public Ship::GuiWindow {
public:
    explicit ViewHostWindow(std::shared_ptr<ViewManager> views)
        : Ship::GuiWindow("gTorchViewer", true, "Torch"), mViews(std::move(views)) {}

    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}

    void Draw() override {
        // Keep everything inside one OS window: never let ImGui spawn platform
        // viewports (the viewer is a single fullscreen window, not a dockspace).
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

// Fast3dGui draws a fullscreen "Main Game" window with the (empty, black) game
// framebuffer on top of our UI. The asset viewer has no game scene, so suppress
// it; 3D previews render into their own ImGui images in the asset panel.
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

        // Load any .o2r archives in the working directory so Fast3D can resolve
        // the resources (DisplayList/Vertex/Texture) referenced by the assets we
        // preview. These are produced by Torch's own o2r export, so the resource
        // hashes match what the display lists reference.
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

        // Register the Fast3D binary resource factories so the ResourceManager can
        // import the resources our display lists reference (DisplayList/Vertex/
        // Texture/Matrix/Light). Without these, LoadResource fails on ODLT etc.
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

        // Construct with no gui windows: the window must be registered with the
        // Context (InitWindow) before any GuiWindow is added, otherwise their
        // setup dereferences Context::GetWindow() before it is set.
        auto gui = std::make_shared<ViewerGui>(std::vector<std::shared_ptr<Ship::GuiWindow>>{});
        auto window = std::make_shared<Fast::Fast3dWindow>(gui);
        ctx->InitWindow(window);
        ctx->InitEventSystem();

        window->GetGui()->AddGuiWindow(std::make_shared<ViewHostWindow>(views));

        window->SetTargetFps(60);
        // Force Fast3D to render into the offscreen game framebuffer (mRendersToFb)
        // rather than straight to the window backbuffer — otherwise the model draws
        // behind our ImGui and GetGfxFrameBuffer() has nothing to show. Enabling
        // MSAA is the trigger (StartFrame: mMsaaLevel > 1) and also looks nicer.
        window->SetMsaaLevel(4);

        // SM64/Torch display lists are F3D (opcodes G_VTX=0x04, G_ENDDL=0xB8) and
        // our gbi.h is F3DEX_GBI, whose macros emit the same F3D opcodes. f3dex2
        // would misread them. Per-DL ucode for mixed archives is a later step.
        window->SetRendererUCode(ucode_f3d);

        ApplyTorchTheme();

        static Gfx emptyDl[] = { gsSPEndDisplayList() };
        while (window->IsRunning()) {
            window->HandleEvents();

            // Visible rows queue a preview request during the gui draw; render the
            // previous frame's queue here, then the gui draw blits the results and
            // queues the next frame.
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
        // Called from factory DrawUI (inside the gui draw), so the rendering API
        // is current. Mirrors Fast3dGui::LoadGuiTexture's GPU upload path.
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

    void DrawPointCloud(uint64_t, const std::vector<PreviewVertex>&, const ImVec2&, const ImVec2&, float, float,
                        float) override {
        // Superseded by DrawModel for GFX assets.
    }

    void DrawModel(const std::string& resourceName, const ImVec2& topLeft, const ImVec2& size, float yaw, float pitch,
                   float zoom) override {
        if (size.x < 1.0f || size.y < 1.0f) {
            return;
        }
        // Queue for next frame's render pass, then blit this model's framebuffer
        // (sized to its rect, so it fills the canvas centered).
        mRequests.push_back({ resourceName, topLeft, size, yaw, pitch, zoom });

        auto it = mNameToFb.find(resourceName);
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

private:
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

    // Walk a display list accumulating vertex bounds, following both vertex loads
    // (G_VTX_OTR_HASH 0x32) and display-list branches (G_DL_OTR_HASH 0x31) so
    // container DLs (which only branch to sub-DLs) are framed by their children.
    // Torch stores the referenced resource hash in the Gfx word after the command.
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

    // A reusable render target for one model preview. Held in a fixed-size pool
    // and reassigned by model name (LRU-ish) as rows scroll in and out of view.
    struct FbSlot {
        int fbId = -1;
        uint32_t w = 0, h = 0;
        std::string owner; // model currently rendered into this framebuffer
        Vp vp{};
        Mtx proj{};        // stable addresses: keys into the Mtx->MtxF replacement map
        Mtx view{};
        // Last view actually rendered into this framebuffer; used to detect when a
        // model needs re-rendering (its FB content persists between frames).
        bool rendered = false;
        float lastYaw = 0.0f, lastPitch = 0.0f, lastZoom = 0.0f;
    };

    // Pick a pool slot for `name`: reuse its existing one, else a slot whose owner
    // isn't visible this frame, else any free slot. -1 if the pool is exhausted.
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

    // Assign a framebuffer to every visible model, then build a command list that
    // renders just the one model whose view changed this frame (G_SETFB/G_RESETFB
    // target it). One framebuffer per frame — see the phase-2 note. Returns nullptr
    // if nothing needs rendering. mNameToFb (built here) lets DrawModel blit each
    // model's persisted framebuffer.
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

        // Phase 1: give every visible model a framebuffer slot (its last render
        // persists to be blitted), and pick the ONE model to (re)render this frame.
        // Only one framebuffer is rendered per frame because this LUS Metal backend
        // only presents the first offscreen framebuffer drawn per frame — rendering
        // N at once left all but the first frozen. Idle models don't change, so
        // their persisted render stays valid; the dragged model is dirty every frame.
        int targetIdx = -1;
        int targetReqIdx = -1;
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

            const bool dirty = !slot.rendered || slot.lastYaw != req.yaw || slot.lastPitch != req.pitch ||
                               slot.lastZoom != req.zoom;
            if (dirty && targetIdx < 0) {
                targetIdx = idx;
                targetReqIdx = (int)ri;
            }
        }

        // Rebuild the name->framebuffer map from the pool's current owners so
        // DrawModel can blit any slot still holding a model (even ones not
        // re-rendered this frame), and evicted owners drop out.
        mNameToFb.clear();
        for (const auto& s : mFbPool) {
            if (s.fbId >= 0 && !s.owner.empty()) {
                mNameToFb[s.owner] = s.fbId;
            }
        }

        if (targetIdx < 0) {
            return nullptr; // nothing changed; every framebuffer keeps its last render
        }

        // Phase 2: render just the target model into its framebuffer.
        const ModelRequest& req = mRenderList[targetReqIdx];
        FbSlot& slot = mFbPool[targetIdx];
        auto dl = std::static_pointer_cast<Fast::DisplayList>(rm->LoadResource(req.name));
        if (dl == nullptr) {
            return nullptr;
        }
        Gfx* model = dl->GetPointer();
        if (model == nullptr) {
            return nullptr;
        }

        auto bit = mBoundsCache.find(req.name);
        if (bit == mBoundsCache.end()) {
            bit = mBoundsCache.emplace(req.name, ComputeBounds(dl, rm)).first;
        }
        const ModelBounds& b = bit->second;

        const float aspect = (float)slot.w / (float)slot.h;
        const float dist = (b.radius * 2.5f) / std::max(req.zoom, 0.02f);
        const float eyeX = b.cx + dist * std::cos(req.pitch) * std::sin(req.yaw);
        const float eyeY = b.cy + dist * std::sin(req.pitch);
        const float eyeZ = b.cz + dist * std::cos(req.pitch) * std::cos(req.yaw);

        MtxF projF{};
        Perspective(projF, 45.0f, aspect, std::max(dist * 0.01f, 1.0f), dist * 4.0f + b.radius * 4.0f);
        MtxF viewF{};
        LookAt(viewF, eyeX, eyeY, eyeZ, b.cx, b.cy, b.cz);
        mMtxReplacements[&slot.proj] = projF;
        mMtxReplacements[&slot.view] = viewF;

        const int16_t vx = (int16_t)(slot.w * 2);
        const int16_t vy = (int16_t)(slot.h * 2);
        slot.vp.vp.vscale[0] = vx; slot.vp.vp.vscale[1] = vy; slot.vp.vp.vscale[2] = G_MAXZ / 2;
        slot.vp.vp.vscale[3] = 0;
        slot.vp.vp.vtrans[0] = vx; slot.vp.vp.vtrans[1] = vy; slot.vp.vp.vtrans[2] = G_MAXZ / 2;
        slot.vp.vp.vtrans[3] = 0;

        mLights = gdSPDefLights1(0x40, 0x40, 0x40, 0xff, 0xff, 0xff, 0x49, 0x49, 0x49);

        mCmd.assign(72, Gfx{});
        Gfx* p = mCmd.data();
        // Load f3d ucode + reset segment 0 (global state).
        p->words.w0 = ((uintptr_t)0xDD << 24) | ((uintptr_t)ucode_f3d & 0xFFFFFF);
        p->words.w1 = 0;
        ++p;
        __gSPSegment(p++, 0, 0x0);
        // Non-zero color image so FILL rects clear color (else GfxDpFillRectangle
        // treats it as a depth clear when color/z image addresses match at 0).
        gDPSetColorImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (void*)(uintptr_t)0x10);

        // Switch the render target to this model's framebuffer (G_SETFB 0x21).
        p->words.w0 = (uintptr_t)0x21 << 24;
        p->words.w1 = (uintptr_t)slot.fbId;
        ++p;
        // Clear color with a flat fill rectangle (G_SETFB already cleared depth to
        // far). The quad is at the near plane, so use a no-Z rendermode or it writes
        // "nearest" depth and occludes the model.
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
        gSPMatrix(p++, &slot.view, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPSetCombineMode(p++, G_CC_MODULATEI, G_CC_MODULATEI_PRIM2);
        gDPSetDepthSource(p++, G_ZS_PIXEL);
        gDPSetCycleType(p++, G_CYC_1CYCLE);
        gDPSetRenderMode(p++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_LIGHTING);
        gSPSetLights1(p++, mLights);
        __gSPDisplayList(p++, model);
        gDPFullSync(p++);
        // Restore the main framebuffer (G_RESETFB 0x22) before the gui draws.
        p->words.w0 = (uintptr_t)0x22 << 24;
        p->words.w1 = 0;
        ++p;
        gSPEndDisplayList(p++);

        slot.rendered = true;
        slot.lastYaw = req.yaw;
        slot.lastPitch = req.pitch;
        slot.lastZoom = req.zoom;
        return mCmd.data();
    }

    static constexpr int kFbPoolSize = 24;

    // A model preview render request, queued by DrawModel during the gui draw.
    struct ModelRequest {
        std::string name;
        ImVec2 topLeft;
        ImVec2 size;
        float yaw = 0.0f, pitch = 0.0f, zoom = 1.0f;
    };

    std::vector<ModelRequest> mRequests;   // filled this frame, rendered next
    std::vector<ModelRequest> mRenderList; // snapshot being rendered this frame
    std::unordered_map<std::string, int> mNameToFb;          // model -> framebuffer id
    std::unordered_map<std::string, ModelBounds> mBoundsCache;
    std::vector<FbSlot> mFbPool;
    Lights1 mLights{};
    std::vector<Gfx> mCmd;
    std::unordered_map<Mtx*, MtxF> mMtxReplacements;
};

} // namespace

std::unique_ptr<BaseBackend> CreateLusBackend() {
    return std::make_unique<LusBackend>();
}

} // namespace UI

#endif // BUILD_UI
