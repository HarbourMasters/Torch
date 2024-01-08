#pragma once

#include "../BaseFactory.h"

namespace MK64 {

    struct CourseVtx {
        int16_t		ob[3];	/* x, y, z */
        int16_t		tc[2];	/* texture coord */
        uint8_t	    cn[4];	/* color & alpha */
    };

    class VtxData : public IParsedData {
    public:
        std::vector<CourseVtx> mVtxs;

        explicit VtxData(std::vector<CourseVtx> vtxs) : mVtxs(vtxs) {}
    };

    class VtxHeaderExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class VtxBinaryExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class VtxCodeExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class VtxFactory : public BaseFactory {
    public:
        std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
        inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
            return {
                REGISTER(Code, VtxCodeExporter)
                REGISTER(Header, VtxHeaderExporter)
                REGISTER(Binary, VtxBinaryExporter)
            };
        }
    };

}