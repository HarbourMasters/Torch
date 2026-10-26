#include "InputSource.h"
#include "N64RomInput.h"

#include <fstream>
#include <stdexcept>

#ifdef GAMECUBE_SUPPORT
#include "gc/GameCubeInput.h"
#endif

std::shared_ptr<InputSource> InputSource::Create(const std::optional<std::filesystem::path>& path,
                                                 std::vector<uint8_t>& data) {
#ifdef GAMECUBE_SUPPORT
    if (path.has_value()) {
        if (auto gc = GC::GameCubeInput::FromPath(path.value())) {
            return gc;
        }
    } else if (auto gc = GC::GameCubeInput::FromMemory(data)) {
        return gc;
    }
#endif

    if (path.has_value()) {
        std::error_code ec;
        if (std::filesystem::is_directory(path.value(), ec)) {
            throw std::runtime_error(path->string() + " is a directory, not a ROM or an extracted GameCube disc");
        }
        std::ifstream input(path.value(), std::ios::binary);
        data = std::vector<uint8_t>(std::istreambuf_iterator(input), {});
        input.close();
    }

    return std::make_shared<N64RomInput>(std::move(data));
}
