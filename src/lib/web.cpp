#ifdef __EMSCRIPTEN__
#include "Companion.h"
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <emscripten/bind.h>
using namespace emscripten;

Companion* Companion::Instance;
static emscripten::val gSinkCallback = emscripten::val::null();

class EMSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (gSinkCallback.isNull() || gSinkCallback.isUndefined()) {
            return;
        }

        // Format the message using the sink's formatter_
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);

        // Send to JS callback
        gSinkCallback(std::string(buf.data(), buf.size()));
    }

    void flush_() override {
        // No-op for JS sink
    }
};

void Bind(Companion* instance) {
    Companion::Instance = instance;
}

void RegisterLogSink(emscripten::val sink) {
    if (sink.isUndefined() || sink.isNull()) {
        // Unregister the current sink
        spdlog::drop_all();
        gSinkCallback = emscripten::val::null();
        return;
    }

    gSinkCallback = sink;
    auto logger = std::make_shared<spdlog::logger>("emscripten_logger", std::make_shared<EMSink>());
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
}

EMSCRIPTEN_BINDINGS(Torch) {
    register_vector<uint8_t>("VectorUInt8");

    function("Bind", &Bind, allow_raw_pointers());
    function("RegisterLogSink", &RegisterLogSink);

    enum_<ExportType>("ExportType")
        .value("Header", ExportType::Header)
        .value("Code", ExportType::Code)
        .value("Binary", ExportType::Binary)
        .value("Modding", ExportType::Modding)
        .value("XML", ExportType::XML);

    enum_<ArchiveType>("ArchiveType")
        .value("None", ArchiveType::None)
        .value("OTR", ArchiveType::OTR)
        .value("O2R", ArchiveType::O2R);

    enum_<N64::CountryCode>("CountryCode")
        .value("Japan", N64::CountryCode::Japan)
        .value("NorthAmerica", N64::CountryCode::NorthAmerica)
        .value("Europe", N64::CountryCode::Europe)
        .value("Unknown", N64::CountryCode::Unknown);

    class_<N64::Cartridge>("Cartridge")
        .constructor<std::vector<uint8_t>>()
        .function("Initialize", &N64::Cartridge::Initialize)
        .function("GetGameTitle", &N64::Cartridge::GetGameTitle)
        .function("GetCountryCode", &N64::Cartridge::GetCountryCode)
        .function("GetCountry", &N64::Cartridge::GetCountry)
        .function("GetVersion", &N64::Cartridge::GetVersion)
        .function("GetHash", &N64::Cartridge::GetHash)
        .function("GetCRC", &N64::Cartridge::GetCRC);

    class_<Companion>("Companion")
        .constructor<std::vector<uint8_t>, ArchiveType, bool, bool, std::string, std::string>()
        .constructor<std::vector<uint8_t>, ArchiveType, bool, bool, std::string>()
        .constructor<std::vector<uint8_t>, ArchiveType, bool, bool>()
        .function("Init", &Companion::Init)
        .function("Process", &Companion::Process)
        .function("Finalize", &Companion::Finalize)
        .function("GetOutputPath", &Companion::GetOutputPath)
        .function("GetCartridge", &Companion::GetCartridge, allow_raw_pointers())
        .function("GetRomData", &Companion::GetRomData, allow_raw_pointers());
}
#endif