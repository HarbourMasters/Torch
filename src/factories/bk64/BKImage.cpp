#include "BKImage.h"
#include "BKByteUtils.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <n64graphics/stb_image.h>
// Defined in stb_image_write.c without a declaration in its header. n64graphics renames them under BUILD_UI.
#ifdef BUILD_UI
#define stbi_write_png_to_mem n64g_stbi_write_png_to_mem
#define stbi_zlib_compress n64g_stbi_zlib_compress
#endif
unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes, int x, int y, int n, int* out_len);
unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);
}

namespace BK64 {

namespace {
const uint8_t kPngMagic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

bool isPng(const std::vector<uint8_t>& file) {
    return file.size() >= 8 && std::memcmp(file.data(), kPngMagic, 8) == 0;
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> out{};
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            out[n] = c;
        }
        return out;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back((uint8_t)(value >> shift));
    }
}

void putChunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    putU32(png, (uint32_t)data.size());
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    putU32(png, crc32(png.data() + start, png.size() - start));
}

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    return (uint8_t)((pa <= pb && pa <= pc) ? a : pb <= pc ? b : c);
}

std::vector<uint8_t> takeMalloced(unsigned char* data, int size) {
    std::vector<uint8_t> out(data, data + size);
    std::free(data);
    return out;
}
} // namespace

bool IsGif(const std::vector<uint8_t>& file) {
    return file.size() >= 6 && std::memcmp(file.data(), "GIF8", 4) == 0;
}

bool DecodeImage(const std::vector<uint8_t>& file, std::vector<RgbaImage>& frames, std::vector<int>* delays,
                 std::string& error) {
    frames.clear();
    if (delays != nullptr) {
        delays->clear();
    }
    int width = 0;
    int height = 0;
    int comp = 0;
    if (IsGif(file)) {
        int* delayList = nullptr;
        int count = 0;
        stbi_uc* data =
            stbi_load_gif_from_memory(file.data(), (int)file.size(), &delayList, &width, &height, &count, &comp, 4);
        if (data == nullptr) {
            error = std::string("the GIF doesn't decode: ") + stbi_failure_reason();
            return false;
        }
        const size_t frameSize = (size_t)width * height * 4;
        for (int i = 0; i < count; i++) {
            frames.push_back({ width, height, std::vector<uint8_t>(data + frameSize * i, data + frameSize * (i + 1)) });
            if (delays != nullptr) {
                delays->push_back(delayList != nullptr ? delayList[i] : 0);
            }
        }
        stbi_image_free(data);
        stbi_image_free(delayList);
        return !frames.empty();
    }
    if (!isPng(file)) {
        error = "it isn't a PNG or a GIF";
        return false;
    }
    stbi_uc* data = stbi_load_from_memory(file.data(), (int)file.size(), &width, &height, &comp, 4);
    if (data == nullptr) {
        error = std::string("the PNG doesn't decode: ") + stbi_failure_reason();
        return false;
    }
    frames.push_back({ width, height, std::vector<uint8_t>(data, data + (size_t)width * height * 4) });
    stbi_image_free(data);
    if (delays != nullptr) {
        delays->push_back(0);
    }
    return true;
}

bool DecodeIndexedPng(const std::vector<uint8_t>& file, IndexedImage& out) {
    if (!isPng(file)) {
        return false;
    }
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<uint8_t> idat, plte, trns;
    for (size_t at = 8; at + 12 <= file.size();) {
        const uint32_t length = ReadU32BE(&file[at]);
        if (length > file.size() - at - 12) {
            return false;
        }
        const char* type = (const char*)&file[at + 4];
        const uint8_t* data = &file[at + 8];
        if (std::memcmp(type, "IHDR", 4) == 0) {
            // Palette color type, no interlace
            if (length < 13 || data[9] != 3 || data[12] != 0) {
                return false;
            }
            width = (int)ReadU32BE(data);
            height = (int)ReadU32BE(data + 4);
            depth = data[8];
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            plte.assign(data, data + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(data, data + length);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + length;
    }
    if (width <= 0 || height <= 0 || plte.size() < 3 || idat.empty() ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8)) {
        return false;
    }
    int rawSize = 0;
    char* raw = stbi_zlib_decode_malloc((const char*)idat.data(), (int)idat.size(), &rawSize);
    if (raw == nullptr) {
        return false;
    }
    const size_t stride = ((size_t)width * depth + 7) / 8;
    if ((size_t)rawSize < (stride + 1) * height) {
        stbi_image_free(raw);
        return false;
    }
    out.width = width;
    out.height = height;
    out.indices.assign((size_t)width * height, 0);
    std::vector<uint8_t> prior(stride, 0), row(stride, 0);
    for (int y = 0; y < height; y++) {
        const uint8_t* src = (const uint8_t*)raw + y * (stride + 1);
        const uint8_t filter = src[0];
        src++;
        for (size_t i = 0; i < stride; i++) {
            const int a = i >= 1 ? row[i - 1] : 0;
            const int b = prior[i];
            const int c = i >= 1 ? prior[i - 1] : 0;
            switch (filter) {
                case 0:
                    row[i] = src[i];
                    break;
                case 1:
                    row[i] = (uint8_t)(src[i] + a);
                    break;
                case 2:
                    row[i] = (uint8_t)(src[i] + b);
                    break;
                case 3:
                    row[i] = (uint8_t)(src[i] + (a + b) / 2);
                    break;
                case 4:
                    row[i] = (uint8_t)(src[i] + paeth(a, b, c));
                    break;
                default:
                    stbi_image_free(raw);
                    return false;
            }
        }
        for (int x = 0; x < width; x++) {
            const size_t bit = (size_t)x * depth;
            out.indices[(size_t)y * width + x] =
                depth == 8 ? row[x] : (uint8_t)((row[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1));
        }
        prior = row;
    }
    stbi_image_free(raw);
    const size_t entries = plte.size() / 3;
    out.palette.clear();
    for (size_t i = 0; i < entries; i++) {
        out.palette.insert(out.palette.end(),
                           { plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2], i < trns.size() ? trns[i] : (uint8_t)255 });
    }
    return true;
}

std::vector<uint8_t> EncodePng(const RgbaImage& image) {
    int size = 0;
    unsigned char* png =
        stbi_write_png_to_mem(image.pixels.data(), image.width * 4, image.width, image.height, 4, &size);
    return png != nullptr ? takeMalloced(png, size) : std::vector<uint8_t>();
}

std::vector<uint8_t> EncodeGrayPng(int width, int height, const std::vector<uint8_t>& channels, bool alpha) {
    const int n = alpha ? 2 : 1;
    int size = 0;
    unsigned char* png = stbi_write_png_to_mem(channels.data(), width * n, width, height, n, &size);
    return png != nullptr ? takeMalloced(png, size) : std::vector<uint8_t>();
}

std::vector<uint8_t> EncodeIndexedPng(const IndexedImage& image) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(image.width + 1) * image.height);
    for (int y = 0; y < image.height; y++) {
        raw.push_back(0);
        const auto row = image.indices.begin() + (size_t)y * image.width;
        raw.insert(raw.end(), row, row + image.width);
    }
    int zSize = 0;
    unsigned char* z = stbi_zlib_compress(raw.data(), (int)raw.size(), &zSize, 8);
    if (z == nullptr) {
        return {};
    }

    std::vector<uint8_t> png(kPngMagic, kPngMagic + 8);
    std::vector<uint8_t> header;
    putU32(header, image.width);
    putU32(header, image.height);
    header.insert(header.end(), { 8, 3, 0, 0, 0 });
    putChunk(png, "IHDR", header);
    const size_t entries = image.palette.size() / 4;
    std::vector<uint8_t> plte, trns;
    for (size_t i = 0; i < entries; i++) {
        plte.insert(plte.end(), image.palette.begin() + i * 4, image.palette.begin() + i * 4 + 3);
        trns.push_back(image.palette[i * 4 + 3]);
    }
    while (!trns.empty() && trns.back() == 255) {
        trns.pop_back();
    }
    putChunk(png, "PLTE", plte);
    if (!trns.empty()) {
        putChunk(png, "tRNS", trns);
    }
    putChunk(png, "IDAT", takeMalloced(z, zSize));
    putChunk(png, "IEND", {});
    return png;
}

IndexedImage Quantize(const RgbaImage& image, int colors) {
    const auto key = [](const uint8_t* p) {
        return (uint16_t)((CompressBits(p[0], 5) << 10) | (CompressBits(p[1], 5) << 5) | CompressBits(p[2], 5));
    };
    const auto channel = [](uint16_t color, int axis) { return (color >> (10 - axis * 5)) & 0x1F; };

    std::vector<uint32_t> counts(1 << 15, 0);
    bool clear = false;
    const size_t pixels = (size_t)image.width * image.height;
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t* p = &image.pixels[i * 4];
        if (p[3] < 128) {
            clear = true;
        } else {
            counts[key(p)]++;
        }
    }
    std::vector<uint16_t> used;
    for (uint32_t color = 0; color < counts.size(); color++) {
        if (counts[color] > 0) {
            used.push_back((uint16_t)color);
        }
    }
    const int room = std::max(1, colors - (clear ? 1 : 0));

    std::vector<uint16_t> chosen;
    if ((int)used.size() <= room) {
        chosen = used;
        std::stable_sort(chosen.begin(), chosen.end(), [&](uint16_t a, uint16_t b) { return counts[a] > counts[b]; });
    } else {
        // Median cut: split the box with the widest channel at its weighted median until there's room for no more
        std::vector<std::vector<uint16_t>> boxes{ used };
        const auto widest = [&](const std::vector<uint16_t>& box, int& axis) {
            int best = -1;
            for (int a = 0; a < 3; a++) {
                int lo = 31, hi = 0;
                for (uint16_t color : box) {
                    lo = std::min(lo, channel(color, a));
                    hi = std::max(hi, channel(color, a));
                }
                if (hi - lo > best) {
                    best = hi - lo;
                    axis = a;
                }
            }
            return best;
        };
        while ((int)boxes.size() < room) {
            int pick = -1, pickAxis = 0, pickRange = 0;
            for (size_t b = 0; b < boxes.size(); b++) {
                int axis = 0;
                const int range = boxes[b].size() > 1 ? widest(boxes[b], axis) : 0;
                if (range > pickRange) {
                    pick = (int)b;
                    pickAxis = axis;
                    pickRange = range;
                }
            }
            if (pick < 0) {
                break;
            }
            auto box = std::move(boxes[pick]);
            std::sort(box.begin(), box.end(),
                      [&](uint16_t a, uint16_t b) { return channel(a, pickAxis) < channel(b, pickAxis); });
            uint64_t total = 0, run = 0;
            for (uint16_t color : box) {
                total += counts[color];
            }
            size_t cut = 1;
            for (; cut < box.size(); cut++) {
                run += counts[box[cut - 1]];
                if (run * 2 >= total) {
                    break;
                }
            }
            cut = std::min(cut, box.size() - 1);
            boxes[pick].assign(box.begin(), box.begin() + cut);
            boxes.emplace_back(box.begin() + cut, box.end());
        }
        for (const auto& box : boxes) {
            uint64_t sum[3] = {}, total = 0;
            for (uint16_t color : box) {
                for (int a = 0; a < 3; a++) {
                    sum[a] += (uint64_t)channel(color, a) * counts[color];
                }
                total += counts[color];
            }
            uint16_t mean = 0;
            for (int a = 0; a < 3; a++) {
                mean |= (uint16_t)((sum[a] + total / 2) / total) << (10 - a * 5);
            }
            chosen.push_back(mean);
        }
    }

    IndexedImage out;
    out.width = image.width;
    out.height = image.height;
    if (clear) {
        out.palette.insert(out.palette.end(), { 0, 0, 0, 0 });
    }
    for (uint16_t color : chosen) {
        out.palette.insert(out.palette.end(), { ExpandBits(channel(color, 0), 5), ExpandBits(channel(color, 1), 5),
                                                ExpandBits(channel(color, 2), 5), 255 });
    }
    const uint8_t first = clear ? 1 : 0;
    std::vector<int16_t> nearest(1 << 15, -1);
    out.indices.assign(pixels, 0);
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t* p = &image.pixels[i * 4];
        if (p[3] < 128) {
            continue;
        }
        const uint16_t color = key(p);
        if (nearest[color] < 0) {
            int best = 0, bestDistance = INT32_MAX;
            for (size_t c = 0; c < chosen.size(); c++) {
                int distance = 0;
                for (int a = 0; a < 3; a++) {
                    const int d = channel(color, a) - channel(chosen[c], a);
                    distance += d * d;
                }
                if (distance < bestDistance) {
                    best = (int)c;
                    bestDistance = distance;
                }
            }
            nearest[color] = (int16_t)(best + first);
        }
        out.indices[i] = (uint8_t)nearest[color];
    }
    return out;
}

} // namespace BK64
