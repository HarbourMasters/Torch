#include "TrajectoryFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <regex>

ExportResult SM64::TrajectoryHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                    std::string& entryName, YAML::Node& node,
                                                    std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Trajectory " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult SM64::TrajectoryCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");

    auto trajectoryData = std::static_pointer_cast<SM64::TrajectoryData>(raw)->mTrajectoryData;

    write << "const Trajectory " << symbol << "[] = {\n";

    for (auto& trajectory : trajectoryData) {
        write << fourSpaceTab;
        if (trajectory.trajId == -1) {
            write << "TRAJECTORY_END(),\n";
            break;
        }
        write << "TRAJECTORY_POS(";
        write << trajectory.trajId << ", " << trajectory.posX << ", " << trajectory.posY << ", " << trajectory.posZ
              << "),\n";
    }

    write << "\n};\n";

    size_t size = (trajectoryData.size()) * sizeof(Trajectory);

    return offset + size;
}

ExportResult SM64::TrajectoryBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                    std::string& entryName, YAML::Node& node,
                                                    std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto trajectoryData = std::static_pointer_cast<SM64::TrajectoryData>(raw)->mTrajectoryData;

    WriteHeader(writer, Torch::ResourceType::Trajectory, 0);

    writer.Write((uint32_t)trajectoryData.size());

    for (auto& trajectory : trajectoryData) {
        writer.Write(trajectory.trajId);
        writer.Write(trajectory.posX);
        writer.Write(trajectory.posY);
        writer.Write(trajectory.posZ);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::TrajectoryFactory::parse(std::vector<uint8_t>& buffer,
                                                                           YAML::Node& node) {
    std::vector<Trajectory> trajectoryData;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);

    SPDLOG_INFO("Parsing MIO0 Trajectory Data, size: {}", segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    bool isRunning = true;
    while (isRunning) {
        auto trajId = reader.ReadInt16();
        if (trajId == -1) {
            break;
        }
        auto posX = reader.ReadInt16();
        auto posY = reader.ReadInt16();
        auto posZ = reader.ReadInt16();
        trajectoryData.emplace_back(trajId, posX, posY, posZ);
    }

    trajectoryData.emplace_back(-1, 0, 0, 0); // End marker

    return std::make_shared<SM64::TrajectoryData>(trajectoryData);
}

#ifdef BUILD_UI
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace {

std::unordered_map<std::string, UI::OrbitView> sTrajectoryViews;

void PushTri(std::vector<UI::PreviewVertex>& out, const float a[3], const float b[3], const float c[3],
             const unsigned char color[4]) {
    const float* pts[3] = { a, b, c };
    for (const auto* p : pts) {
        UI::PreviewVertex v{};
        std::memcpy(v.position, p, sizeof(v.position));
        std::memcpy(v.color, color, 4);
        out.push_back(v);
    }
}

// Each segment becomes two crossed ribbons so the path reads from any angle;
// color runs green (start) to red (end).
const std::vector<UI::PreviewVertex>& TrajectoryTris(const ParseResultData& item) {
    static std::unordered_map<std::string, std::vector<UI::PreviewVertex>> sCache;
    auto it = sCache.find(item.name);
    if (it != sCache.end()) {
        return it->second;
    }

    auto data = std::static_pointer_cast<SM64::TrajectoryData>(item.data.value());
    const auto& pts = data->mTrajectoryData;

    std::vector<UI::PreviewVertex> tris;
    float mn[3] = { 1e18f, 1e18f, 1e18f }, mx[3] = { -1e18f, -1e18f, -1e18f };
    for (const auto& p : pts) {
        const float pos[3] = { (float)p.posX, (float)p.posY, (float)p.posZ };
        for (int k = 0; k < 3; ++k) {
            mn[k] = std::min(mn[k], pos[k]);
            mx[k] = std::max(mx[k], pos[k]);
        }
    }
    const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    const float w = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) * 0.008f, 2.0f);

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const float a[3] = { (float)pts[i].posX, (float)pts[i].posY, (float)pts[i].posZ };
        const float b[3] = { (float)pts[i + 1].posX, (float)pts[i + 1].posY, (float)pts[i + 1].posZ };
        float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 0.0001f) {
            continue;
        }
        for (float& v : d) {
            v /= len;
        }
        float u[3] = { d[2], 0.0f, -d[0] };
        const float ulen = std::sqrt(u[0] * u[0] + u[2] * u[2]);
        if (ulen > 0.0001f) {
            u[0] = u[0] / ulen * w;
            u[2] = u[2] / ulen * w;
        } else {
            u[0] = w;
            u[2] = 0.0f;
        }
        const float v2[3] = { (d[1] * u[2] - d[2] * u[1]), (d[2] * u[0] - d[0] * u[2]), (d[0] * u[1] - d[1] * u[0]) };

        const float t = pts.size() > 2 ? (float)i / (float)(pts.size() - 2) : 0.0f;
        const unsigned char color[4] = { (unsigned char)(60 + 195 * t), (unsigned char)(220 - 160 * t), 60, 255 };

        const float* offs[2] = { u, v2 };
        for (const auto* o : offs) {
            const float q0[3] = { a[0] - o[0], a[1] - o[1], a[2] - o[2] };
            const float q1[3] = { a[0] + o[0], a[1] + o[1], a[2] + o[2] };
            const float q2[3] = { b[0] + o[0], b[1] + o[1], b[2] + o[2] };
            const float q3[3] = { b[0] - o[0], b[1] - o[1], b[2] - o[2] };
            PushTri(tris, q0, q1, q2, color);
            PushTri(tris, q0, q2, q3, color);
        }
    }
    return sCache.emplace(item.name, std::move(tris)).first->second;
}

} // namespace

float SM64::TrajectoryFactoryUI::GetItemHeight(const ParseResultData& item) {
    return ImGui::GetTextLineHeightWithSpacing() * 2.0f + UI::PreviewBlockHeight(item.name) + 4.0f +
           ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void SM64::TrajectoryFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    auto data = std::static_pointer_cast<SM64::TrajectoryData>(item.data.value());
    ImGui::TextDisabled("trajectory  \xe2\x80\x94  %zu points, green start to red end", data->mTrajectoryData.size());

    UI::OrbitView& view = sTrajectoryViews[item.name];
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##trajview", item.name, view);
    if (canvas.visible) {
        const auto& tris = TrajectoryTris(item);
        if (tris.empty()) {
            const char* label = "not enough points";
            const ImVec2 ts = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(ImVec2(canvas.origin.x + (canvas.size.x - ts.x) * 0.5f,
                                                       canvas.origin.y + (canvas.size.y - ts.y) * 0.5f),
                                                IM_COL32(120, 120, 130, 255), label);
        } else {
            UI::GetBackend()->DrawTriangles(item.name, tris, canvas.origin, canvas.size, view);
        }
    }
}
#endif // BUILD_UI
