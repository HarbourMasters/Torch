#include "SF64Preview.h"

#ifdef BUILD_UI

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "AnimFactory.h"
#include "ColPolyFactory.h"
#include "Companion.h"
#include "HitboxFactory.h"
#include "MessageFactory.h"
#include "SkeletonFactory.h"
#include "TriangleFactory.h"
#include "factories/Vec3fFactory.h"
#include "factories/Vec3sFactory.h"
#include "imgui.h"
#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace SF64 {
namespace {

const std::string* OwningFile(const ParseResultData& item) {
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (&r == &item) {
                return &file;
            }
        }
    }
    return nullptr;
}

// Row-vector 4x4 (v' = v * M), translation in row 3 — matches the backend.
using Mat4 = float[4][4];

void MatIdentity(Mat4 m) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
}

void MatMul(Mat4 out, const Mat4 a, const Mat4 b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    std::memcpy(out, r, sizeof(r));
}

// Limb local transform: rotate (radians) then translate, row-vector form.
// Rotation order matches the game (RotateZ, RotateY, RotateX post-multiplied,
// i.e. a point is rotated X first): R = Rx * Ry * Rz in row-vector form.
void LimbMatrix(Mat4 m, const Vec3f& t, float rx, float ry, float rz) {
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);
    m[0][0] = cy * cz;
    m[0][1] = cy * sz;
    m[0][2] = -sy;
    m[0][3] = 0.0f;
    m[1][0] = sx * sy * cz - cx * sz;
    m[1][1] = sx * sy * sz + cx * cz;
    m[1][2] = sx * cy;
    m[1][3] = 0.0f;
    m[2][0] = cx * sy * cz + sx * sz;
    m[2][1] = cx * sy * sz - sx * cz;
    m[2][2] = cx * cy;
    m[2][3] = 0.0f;
    m[3][0] = t.x;
    m[3][1] = t.y;
    m[3][2] = t.z;
    m[3][3] = 1.0f;
}

// Resolved skeleton structure (built once); limb transforms are recomputed per
// frame from a joint table so animations can drive them.
struct SkeletonModel {
    std::shared_ptr<SkeletonData> skel;
    std::string file;
    std::unordered_map<uint32_t, const LimbData*> byAddr;
    std::unordered_map<uint32_t, std::string> resource; // limb addr -> GFX path
    uint32_t rootAddr = 0;
    size_t limbCount = 0;
    size_t meshCount = 0;
};

// Per-limb rotation source. jointTable is in the game's frame-table layout:
// index 0 = root translation (raw), 1..N = limb rotations in degrees. Empty =
// bind pose (limb.mRot binary angles).
struct Pose {
    const std::vector<Vec3f>* jointTable = nullptr;
};

void WalkLimb(const SkeletonModel& model, uint32_t addr, const Mat4 parent, const Pose& pose,
              std::vector<UI::ModelPart>& out, std::unordered_map<uint32_t, int>& visited, int depth) {
    while (addr != 0 && depth < 256) {
        if (++visited[addr] > 1) {
            break; // cycle guard
        }
        const auto it = model.byAddr.find(addr);
        if (it == model.byAddr.end()) {
            break;
        }
        const LimbData& limb = *it->second;

        float rx, ry, rz;
        if (pose.jointTable != nullptr) {
            // jointTable[limbIndex] with limbIndex = array position + 1.
            const size_t idx = (size_t)limb.mIndex + 1;
            const Vec3f r = idx < pose.jointTable->size() ? (*pose.jointTable)[idx] : Vec3f(0, 0, 0);
            rx = r.x * (float)M_PI / 180.0f;
            ry = r.y * (float)M_PI / 180.0f;
            rz = r.z * (float)M_PI / 180.0f;
        } else {
            rx = limb.mRot.x * (float)M_PI / 32768.0f;
            ry = limb.mRot.y * (float)M_PI / 32768.0f;
            rz = limb.mRot.z * (float)M_PI / 32768.0f;
        }
        // The root limb can take an animated translation from jointTable[0].
        Vec3f trans = limb.mTrans;
        if (addr == model.rootAddr && pose.jointTable != nullptr && !pose.jointTable->empty()) {
            trans = (*pose.jointTable)[0];
        }

        Mat4 local, world;
        LimbMatrix(local, trans, rx, ry, rz);
        MatMul(world, local, parent);

        const auto rit = model.resource.find(addr);
        if (rit != model.resource.end()) {
            UI::ModelPart part;
            part.resource = rit->second;
            part.layer = 1;
            std::memcpy(part.mtx, world, sizeof(world));
            out.push_back(std::move(part));
        }
        if (limb.mChild != 0) {
            WalkLimb(model, limb.mChild, world, pose, out, visited, depth + 1);
        }
        addr = limb.mSibling; // iterate siblings without deepening the stack
    }
}

std::vector<UI::ModelPart> BuildParts(const SkeletonModel& model, const Pose& pose) {
    std::vector<UI::ModelPart> parts;
    if (model.rootAddr == 0) {
        return parts;
    }
    Mat4 identity;
    MatIdentity(identity);
    std::unordered_map<uint32_t, int> visited;
    WalkLimb(model, model.rootAddr, identity, pose, parts, visited, 0);
    return parts;
}

const SkeletonModel& BuildSkeleton(const ParseResultData& item) {
    static std::map<std::string, SkeletonModel> sCache;
    const auto cached = sCache.find(item.name);
    if (cached != sCache.end()) {
        return cached->second;
    }
    SkeletonModel model;
    model.skel = std::static_pointer_cast<SkeletonData>(item.data.value());
    model.limbCount = model.skel->mSkeleton.size();
    const std::string* file = OwningFile(item);
    if (file != nullptr && !model.skel->mSkeleton.empty()) {
        model.file = *file;
        model.rootAddr = model.skel->mSkeleton.front().mAddr;
        for (const auto& limb : model.skel->mSkeleton) {
            model.byAddr[limb.mAddr] = &limb;
            if (limb.mDList != 0) {
                auto node = Companion::Instance->GetNodeByAddr(limb.mDList, *file);
                if (node.has_value()) {
                    auto type = GetSafeNode<std::string>(std::get<1>(node.value()), "type", "");
                    std::transform(type.begin(), type.end(), type.begin(), ::toupper);
                    if (type == "GFX") {
                        model.resource[limb.mAddr] = std::get<0>(node.value());
                    }
                }
            }
        }
        model.meshCount = model.resource.size();
    }
    return sCache.emplace(item.name, std::move(model)).first->second;
}

// Ports Animation_GetFrameData: index 0 = root translation (raw s16), 1..limbCount
// = per-limb rotations in degrees. len<=frame clamps to the last (constant) key.
std::vector<Vec3f> ComputeJointTable(const AnimData& anim, int frame) {
    std::vector<Vec3f> table((size_t)anim.mLimbCount + 1);
    const auto& fd = anim.mFrameData;
    const auto val = [&](uint16_t len, uint16_t idx) -> int16_t {
        const uint32_t i = (uint32_t)frame < len ? (uint32_t)idx + frame : idx;
        return i < fd.size() ? (int16_t)fd[i] : 0;
    };
    for (size_t i = 0; i < table.size() && i < anim.mJointKeys.size(); ++i) {
        const auto& k = anim.mJointKeys[i].keys; // {xLen, x, yLen, y, zLen, z}
        const int16_t x = val(k[0], k[1]);
        const int16_t y = val(k[2], k[3]);
        const int16_t z = val(k[4], k[5]);
        if (i == 0) {
            table[0] = Vec3f(x, y, z); // root translation, raw units
        } else {
            table[i] = Vec3f(x * 360.0f / 65536.0f, y * 360.0f / 65536.0f, z * 360.0f / 65536.0f);
        }
    }
    return table;
}

struct AnimEntry {
    std::string name;
    std::shared_ptr<AnimData> data;
};

// SF64 pairs anims to skeletons in code, not data. Offer anims from the same
// file whose limb count fits this skeleton (a strong, reliable filter).
std::vector<AnimEntry> CollectAnims(const std::string& file, size_t limbCount) {
    std::vector<AnimEntry> anims;
    const auto& results = Companion::Instance->GetParseResults();
    const auto it = results.find(file);
    if (it == results.end()) {
        return anims;
    }
    for (const auto& r : it->second) {
        if (r.type != "SF64:ANIM" || !r.data.has_value()) {
            continue;
        }
        auto data = std::static_pointer_cast<AnimData>(r.data.value());
        if ((size_t)data->mLimbCount + 1 >= limbCount && (size_t)data->mLimbCount <= limbCount + 1) {
            anims.push_back({ r.name, data });
        }
    }
    return anims;
}

struct SkelState {
    UI::OrbitView view;
    int setup = -1; // resolved from config on first draw (fallback: textured + lit)
    int animIndex = -1; // -1 = bind pose
    float frame = 0.0f;
    bool playing = true;
};
std::map<std::string, SkelState> sSkelState;

} // namespace

float SkeletonFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 60.0f + UI::PreviewBlockHeight(item.name);
}

void SkeletonFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const SkeletonModel& model = BuildSkeleton(item);
    if (model.meshCount == 0) {
        ImGui::TextDisabled("skeleton  \xe2\x80\x94  %zu limbs, no drawable meshes", model.limbCount);
        return;
    }
    const std::vector<AnimEntry> anims = CollectAnims(model.file, model.limbCount);
    ImGui::TextDisabled("skeleton  \xe2\x80\x94  %zu limbs, %zu meshes, %zu anims", model.limbCount, model.meshCount,
                        anims.size());

    SkelState& st = sSkelState[item.name];
    if (st.setup < 0) {
        const int cfg = UI::ShadeSetupIndexByName(Companion::Instance->GetConfig().defaultShading);
        st.setup = cfg >= 0 ? cfg : 2; // fallback: textured + lit
    }

    UI::ShadeSetupCombo("##sf64setup", st.setup);
    ImGui::SameLine();
    UI::LightingControls();

    st.animIndex = std::clamp(st.animIndex, -1, (int)anims.size() - 1);
    const std::string animLabel = st.animIndex < 0 ? "bind pose" : anims[st.animIndex].name;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##sf64anim", animLabel.c_str())) {
        if (ImGui::Selectable("bind pose", st.animIndex < 0)) {
            st.animIndex = -1;
            st.frame = 0.0f;
        }
        for (int i = 0; i < (int)anims.size(); ++i) {
            if (ImGui::Selectable(anims[i].name.c_str(), st.animIndex == i)) {
                st.animIndex = i;
                st.frame = 0.0f;
            }
        }
        ImGui::EndCombo();
    }

    const AnimData* anim = st.animIndex >= 0 ? anims[st.animIndex].data.get() : nullptr;
    const int frameCount = anim != nullptr ? std::max<int>(1, anim->mFrameCount) : 1;
    if (anim != nullptr) {
        ImGui::SameLine();
        if (ImGui::SmallButton(st.playing ? "Pause##sf64" : "Play##sf64")) {
            st.playing = !st.playing;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("##sf64frame", &st.frame, 0.0f, (float)(frameCount - 1), "frame %.0f");
        if (st.playing) {
            st.frame += ImGui::GetIO().DeltaTime * 30.0f;
            if (st.frame >= (float)frameCount) {
                st.frame = 0.0f;
            }
        }
    }

    std::vector<Vec3f> jointTable;
    Pose pose;
    if (anim != nullptr) {
        jointTable = ComputeJointTable(*anim, std::clamp((int)st.frame, 0, frameCount - 1));
        pose.jointTable = &jointTable;
    }
    std::vector<UI::ModelPart> parts = BuildParts(model, pose);

    const UI::ShadeSetup shade = UI::ShadeSetupFor(st.setup);
    for (auto& part : parts) {
        part.gameShade = shade.gameShade;
        part.unlit = shade.unlit;
        part.fullAmbient = shade.fullAmbient;
    }
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##sf64skel", item.name, st.view);
    if (canvas.visible) {
        UI::GetBackend()->DrawModelParts(item.name, parts, canvas.origin, canvas.size, st.view);
    }
}

float MessageFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() + 120.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void MessageFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto msg = std::static_pointer_cast<MessageData>(item.data.value());
    ImGui::TextDisabled("message  \xe2\x80\x94  %zu chars", msg->mMessage.size());
    ImGui::BeginChild("##sf64msg", ImVec2(0, 100.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(msg->mMesgStr.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

namespace {

// Finds a parsed vertex array in the asset's file by segment offset.
const std::vector<Vec3s>* ResolveVec3s(const std::string& file, uint32_t offset) {
    const auto& results = Companion::Instance->GetParseResults();
    const auto it = results.find(file);
    if (it == results.end()) {
        return nullptr;
    }
    for (const auto& r : it->second) {
        YAML::Node n = r.node;
        if (r.type == "VEC3S" && r.data.has_value() && GetSafeNode<uint32_t>(n, "offset", 0) == offset) {
            return &std::static_pointer_cast<Vec3sData>(r.data.value())->mVecs;
        }
    }
    return nullptr;
}

const std::vector<Vec3f>* ResolveVec3f(const std::string& file, uint32_t offset) {
    const auto& results = Companion::Instance->GetParseResults();
    const auto it = results.find(file);
    if (it == results.end()) {
        return nullptr;
    }
    for (const auto& r : it->second) {
        YAML::Node n = r.node;
        if (r.type == "VEC3F" && r.data.has_value() && GetSafeNode<uint32_t>(n, "offset", 0) == offset) {
            return &std::static_pointer_cast<Vec3fData>(r.data.value())->mVecs;
        }
    }
    return nullptr;
}

// Normal-shaded triangle from three positions, tinted with a base color.
void EmitTri(std::vector<UI::PreviewVertex>& out, const float p[3][3], float r, float g, float b) {
    const float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
    const float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl > 0.0001f) {
        nx /= nl;
        ny /= nl;
        nz /= nl;
    }
    const float light = 0.55f + 0.45f * std::max(0.0f, nx * 0.3f + ny * 0.8f + nz * 0.52f);
    for (int k = 0; k < 3; ++k) {
        UI::PreviewVertex pv{};
        pv.position[0] = p[k][0];
        pv.position[1] = p[k][1];
        pv.position[2] = p[k][2];
        pv.color[0] = (unsigned char)std::clamp(r * light * 255.0f, 0.0f, 255.0f);
        pv.color[1] = (unsigned char)std::clamp(g * light * 255.0f, 0.0f, 255.0f);
        pv.color[2] = (unsigned char)std::clamp(b * light * 255.0f, 0.0f, 255.0f);
        pv.color[3] = 255;
        out.push_back(pv);
    }
}

std::map<std::string, std::vector<UI::PreviewVertex>> sMeshTris;
std::map<std::string, UI::OrbitView> sMeshViews;

// Shared body for the two mesh viewers.
void DrawMeshViewer(const ParseResultData& item, const char* label, size_t polyCount,
                    const std::vector<UI::PreviewVertex>& tris) {
    UI::AssetHeader(item.name, item.type);
    ImGui::TextDisabled("%s  \xe2\x80\x94  %zu triangles", label, polyCount);
    if (tris.empty()) {
        ImGui::TextDisabled("no resolvable mesh vertices");
        return;
    }
    UI::OrbitView& view = sMeshViews[item.name];
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##sf64mesh", item.name, view);
    if (canvas.visible) {
        UI::GetBackend()->DrawTriangles(item.name, tris, canvas.origin, canvas.size, view);
    }
}

} // namespace

float ColPolyFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 40.0f + UI::PreviewBlockHeight(item.name);
}

void ColPolyFactoryUI::DrawUI(const ParseResultData& item) {
    if (!item.data.has_value()) {
        UI::AssetHeader(item.name, item.type);
        ImGui::TextDisabled("no data");
        return;
    }
    auto cached = sMeshTris.find(item.name);
    if (cached == sMeshTris.end()) {
        std::vector<UI::PreviewVertex> tris;
        const auto data = std::static_pointer_cast<ColPolyData>(item.data.value());
        const std::string* file = OwningFile(item);
        const std::vector<Vec3s>* verts = nullptr;
        if (file != nullptr && !data->mMeshNodes.empty()) {
            YAML::Node mn = data->mMeshNodes.front();
            verts = ResolveVec3s(*file, GetSafeNode<uint32_t>(mn, "offset", 0));
        }
        if (verts != nullptr) {
            for (const auto& poly : data->mPolys) {
                const int16_t idx[3] = { poly.tri.x, poly.tri.y, poly.tri.z };
                float p[3][3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    if (idx[k] < 0 || (size_t)idx[k] >= verts->size()) {
                        ok = false;
                        break;
                    }
                    p[k][0] = (*verts)[idx[k]].x;
                    p[k][1] = (*verts)[idx[k]].y;
                    p[k][2] = (*verts)[idx[k]].z;
                }
                if (ok) {
                    EmitTri(tris, p, 0.45f, 0.75f, 0.95f); // collision blue
                }
            }
        }
        cached = sMeshTris.emplace(item.name, std::move(tris)).first;
    }
    DrawMeshViewer(item, "collision mesh", cached->second.size() / 3, cached->second);
}

float TriangleFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 40.0f + UI::PreviewBlockHeight(item.name);
}

void TriangleFactoryUI::DrawUI(const ParseResultData& item) {
    if (!item.data.has_value()) {
        UI::AssetHeader(item.name, item.type);
        ImGui::TextDisabled("no data");
        return;
    }
    auto cached = sMeshTris.find(item.name);
    if (cached == sMeshTris.end()) {
        std::vector<UI::PreviewVertex> tris;
        const auto data = std::static_pointer_cast<TriangleData>(item.data.value());
        const std::string* file = OwningFile(item);
        const std::vector<Vec3f>* verts = nullptr;
        if (file != nullptr && !data->mMeshNodes.empty()) {
            YAML::Node mn = data->mMeshNodes.front();
            verts = ResolveVec3f(*file, GetSafeNode<uint32_t>(mn, "offset", 0));
        }
        if (verts != nullptr) {
            for (const auto& tri : data->mTris) {
                const int16_t idx[3] = { tri.x, tri.y, tri.z };
                float p[3][3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    if (idx[k] < 0 || (size_t)idx[k] >= verts->size()) {
                        ok = false;
                        break;
                    }
                    p[k][0] = (*verts)[idx[k]].x;
                    p[k][1] = (*verts)[idx[k]].y;
                    p[k][2] = (*verts)[idx[k]].z;
                }
                if (ok) {
                    EmitTri(tris, p, 0.6f, 0.85f, 0.5f); // mesh green
                }
            }
        }
        cached = sMeshTris.emplace(item.name, std::move(tris)).first;
    }
    DrawMeshViewer(item, "triangle mesh", cached->second.size() / 3, cached->second);
}

namespace {

// Appends a rotated axis-aligned box (12 tris) centered at c with half-extents h.
void EmitBox(std::vector<UI::PreviewVertex>& out, const float c[3], const float h[3], const float rotDeg[3],
             float r, float g, float b) {
    const float rx = rotDeg[0] * (float)M_PI / 180.0f;
    const float ry = rotDeg[1] * (float)M_PI / 180.0f;
    const float rz = rotDeg[2] * (float)M_PI / 180.0f;
    const float cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz),
                sz = std::sin(rz);
    const auto xform = [&](float x, float y, float z, float out3[3]) {
        // Rz * Ry * Rx applied to the local corner, then translate to center.
        float y1 = cx * y - sx * z, z1 = sx * y + cx * z;
        float x2 = cy * x + sy * z1, z2 = -sy * x + cy * z1;
        float x3 = cz * x2 - sz * y1, y3 = sz * x2 + cz * y1;
        out3[0] = c[0] + x3;
        out3[1] = c[1] + y3;
        out3[2] = c[2] + z2;
    };
    float v[8][3];
    int n = 0;
    for (int sxi = -1; sxi <= 1; sxi += 2) {
        for (int syi = -1; syi <= 1; syi += 2) {
            for (int szi = -1; szi <= 1; szi += 2) {
                xform(sxi * h[0], syi * h[1], szi * h[2], v[n++]);
            }
        }
    }
    // corner index = (xi<<2)|(yi<<1)|zi with -1->0, +1->1
    static const int faces[12][3] = { { 0, 1, 3 }, { 0, 3, 2 }, { 4, 7, 5 }, { 4, 6, 7 },
                                      { 0, 4, 5 }, { 0, 5, 1 }, { 2, 3, 7 }, { 2, 7, 6 },
                                      { 0, 2, 6 }, { 0, 6, 4 }, { 1, 5, 7 }, { 1, 7, 3 } };
    for (const auto& f : faces) {
        float p[3][3];
        for (int k = 0; k < 3; ++k) {
            p[k][0] = v[f[k]][0];
            p[k][1] = v[f[k]][1];
            p[k][2] = v[f[k]][2];
        }
        EmitTri(out, p, r, g, b);
    }
}

std::map<std::string, std::vector<UI::PreviewVertex>> sHitboxTris;
std::map<std::string, UI::OrbitView> sHitboxViews;

} // namespace

float HitboxFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 40.0f + UI::PreviewBlockHeight(item.name);
}

void HitboxFactoryUI::DrawUI(const ParseResultData& item) {
    if (!item.data.has_value()) {
        UI::AssetHeader(item.name, item.type);
        ImGui::TextDisabled("no data");
        return;
    }
    auto cached = sHitboxTris.find(item.name);
    if (cached == sHitboxTris.end()) {
        std::vector<UI::PreviewVertex> tris;
        const auto data = std::static_pointer_cast<HitboxData>(item.data.value());
        const auto& d = data->mData;
        // Box floats {z.off, z.size, y.off, y.size, x.off, x.size}; the entry
        // stride depends on type (see HitboxData parse).
        size_t idx = 1; // skip count
        static const float kHue[5][3] = {
            { 0.9f, 0.4f, 0.4f }, { 0.4f, 0.9f, 0.5f }, { 0.5f, 0.6f, 0.95f },
            { 0.9f, 0.85f, 0.4f }, { 0.85f, 0.5f, 0.9f },
        };
        for (size_t t = 0; t < data->mTypes.size(); ++t) {
            const int type = data->mTypes[t];
            float rot[3] = { 0, 0, 0 };
            size_t box = idx;
            if (type == 2) { // rotated: typecode + 3 rot, then 6 box
                rot[0] = idx + 1 < d.size() ? d[idx + 1] : 0.0f;
                rot[1] = idx + 2 < d.size() ? d[idx + 2] : 0.0f;
                rot[2] = idx + 3 < d.size() ? d[idx + 3] : 0.0f;
                box = idx + 4;
                idx += 10;
            } else if (type == 3 || type == 4) { // shadow/whoosh: typecode + 6 box
                box = idx + 1;
                idx += 7;
            } else { // standard: 6 box floats (first is z.off)
                box = idx;
                idx += 6;
            }
            if (box + 6 > d.size()) {
                break;
            }
            const float c[3] = { d[box + 4], d[box + 2], d[box + 0] };  // x,y,z offset
            const float h[3] = { d[box + 5], d[box + 3], d[box + 1] };  // x,y,z size
            if (h[0] == 0.0f && h[1] == 0.0f && h[2] == 0.0f) {
                continue;
            }
            const float* col = kHue[(type - 1) & 3];
            EmitBox(tris, c, h, rot, col[0], col[1], col[2]);
        }
        cached = sHitboxTris.emplace(item.name, std::move(tris)).first;
    }

    UI::AssetHeader(item.name, item.type);
    const auto data = std::static_pointer_cast<HitboxData>(item.data.value());
    ImGui::TextDisabled("hitbox  \xe2\x80\x94  %zu boxes", data->mTypes.size());
    if (cached->second.empty()) {
        ImGui::TextDisabled("no drawable boxes");
        return;
    }
    UI::OrbitView& view = sHitboxViews[item.name];
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##sf64hitbox", item.name, view);
    if (canvas.visible) {
        UI::GetBackend()->DrawTriangles(item.name, cached->second, canvas.origin, canvas.size, view);
    }
}

} // namespace SF64

#endif // BUILD_UI
