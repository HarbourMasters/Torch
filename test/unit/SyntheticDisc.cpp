#include "SyntheticDisc.h"
#include "TestUtils.h"

#include <functional>
#include <stdexcept>

using namespace TestUtils;

namespace Synthetic {

GC::RelRelocation Rel(uint16_t offset, uint8_t type, uint8_t section, uint32_t addend) {
    return { offset, type, section, addend };
}

std::vector<uint8_t> BuildRel(const RelSpec& spec) {
    std::vector<uint8_t> rel(0x48, 0);
    const uint32_t numSections = static_cast<uint32_t>(spec.sections.size());
    PutBE32(rel, 0x00, spec.id);
    PutBE32(rel, 0x0C, numSections);
    PutBE32(rel, 0x10, 0x48);
    PutBE32(rel, 0x1C, 2);
    PutBE32(rel, 0x40, 32);
    PutBE32(rel, 0x44, 32);
    rel.resize(0x48 + numSections * 8, 0);

    uint32_t bss = 0;
    for (uint32_t i = 0; i < numSections; i++) {
        const auto& s = spec.sections[i];
        if (!s.data.empty()) {
            Align(rel, 0x20);
            const uint32_t offset = static_cast<uint32_t>(rel.size());
            rel.insert(rel.end(), s.data.begin(), s.data.end());
            PutBE32(rel, 0x48 + i * 8, offset | (s.exec ? 1 : 0));
            PutBE32(rel, 0x48 + i * 8 + 4, static_cast<uint32_t>(s.data.size()));
        } else if (s.bssSize != 0) {
            PutBE32(rel, 0x48 + i * 8 + 4, s.bssSize);
            bss += s.bssSize;
        }
    }
    PutBE32(rel, 0x20, bss);

    Align(rel, 4);
    const uint32_t impOffset = static_cast<uint32_t>(rel.size());
    rel.resize(rel.size() + spec.imports.size() * 8, 0);
    PutBE32(rel, 0x28, impOffset);
    PutBE32(rel, 0x2C, static_cast<uint32_t>(spec.imports.size() * 8));
    PutBE32(rel, 0x24, static_cast<uint32_t>(rel.size()));

    for (size_t i = 0; i < spec.imports.size(); i++) {
        PutBE32(rel, impOffset + i * 8, spec.imports[i].first);
        PutBE32(rel, impOffset + i * 8 + 4, static_cast<uint32_t>(rel.size()));
        for (const auto& r : spec.imports[i].second) {
            const size_t at = rel.size();
            rel.resize(at + 8);
            PutBE16(rel, at, r.offset);
            rel[at + 2] = r.type;
            rel[at + 3] = r.section;
            PutBE32(rel, at + 4, r.addend);
        }
    }
    return rel;
}

std::vector<uint8_t> Yay0(const std::vector<uint8_t>& data) {
    const uint32_t maskWords = static_cast<uint32_t>((data.size() + 31) / 32);
    std::vector<uint8_t> out = { 'Y', 'a', 'y', '0' };
    AppendBE32(out, static_cast<uint32_t>(data.size()));
    AppendBE32(out, 16 + maskWords * 4);
    AppendBE32(out, 16 + maskWords * 4);
    for (uint32_t i = 0; i < maskWords; i++) {
        AppendBE32(out, 0xFFFFFFFF);
    }
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<uint8_t> BuildArc(const std::vector<ArcNode>& nodes) {
    std::string strings;
    std::map<std::string, uint32_t> stringOffsets;
    auto str = [&](const std::string& s) {
        if (!stringOffsets.contains(s)) {
            stringOffsets[s] = static_cast<uint32_t>(strings.size());
            strings += s;
            strings.push_back('\0');
        }
        return stringOffsets[s];
    };

    std::vector<uint8_t> nodeTable;
    std::vector<uint8_t> entryTable;
    std::vector<uint8_t> data;
    uint32_t entryIndex = 0;
    uint16_t fileId = 0;

    for (const auto& node : nodes) {
        const size_t at = nodeTable.size();
        nodeTable.resize(at + 0x10);
        for (int i = 0; i < 4; i++) {
            nodeTable[at + i] = node.type[i];
        }
        PutBE32(nodeTable, at + 4, str(node.name));
        PutBE16(nodeTable, at + 0x0A, static_cast<uint16_t>(node.entries.size()));
        PutBE32(nodeTable, at + 0x0C, entryIndex);

        for (const auto& e : node.entries) {
            const size_t ea = entryTable.size();
            entryTable.resize(ea + 0x14);
            const bool dir = e.flags & 0x02;
            PutBE16(entryTable, ea, dir ? 0xFFFF : fileId++);
            PutBE32(entryTable, ea + 4, (uint32_t(e.flags) << 24) | str(e.name));
            if (dir) {
                PutBE32(entryTable, ea + 8, e.node);
                PutBE32(entryTable, ea + 0x0C, 0x10);
            } else {
                Align(data, 0x20);
                PutBE32(entryTable, ea + 8, static_cast<uint32_t>(data.size()));
                PutBE32(entryTable, ea + 0x0C, static_cast<uint32_t>(e.data.size()));
                data.insert(data.end(), e.data.begin(), e.data.end());
            }
            entryIndex++;
        }
    }

    std::vector<uint8_t> arc(0x40, 0);
    arc[0] = 'R', arc[1] = 'A', arc[2] = 'R', arc[3] = 'C';
    PutBE32(arc, 0x08, 0x20);
    PutBE32(arc, 0x20, static_cast<uint32_t>(nodes.size()));
    PutBE32(arc, 0x24, 0x20);
    arc.insert(arc.end(), nodeTable.begin(), nodeTable.end());
    Align(arc, 0x20);
    PutBE32(arc, 0x28, entryIndex);
    PutBE32(arc, 0x2C, static_cast<uint32_t>(arc.size() - 0x20));
    arc.insert(arc.end(), entryTable.begin(), entryTable.end());
    Align(arc, 0x20);
    PutBE32(arc, 0x30, static_cast<uint32_t>(strings.size()));
    PutBE32(arc, 0x34, static_cast<uint32_t>(arc.size() - 0x20));
    arc.insert(arc.end(), strings.begin(), strings.end());
    Align(arc, 0x20);
    PutBE32(arc, 0x0C, static_cast<uint32_t>(arc.size() - 0x20));
    PutBE32(arc, 0x10, static_cast<uint32_t>(data.size()));
    PutBE32(arc, 0x14, static_cast<uint32_t>(data.size()));
    arc.insert(arc.end(), data.begin(), data.end());
    PutBE32(arc, 0x04, static_cast<uint32_t>(arc.size()));
    return arc;
}

std::vector<uint8_t> BuildDol(const std::vector<DolSectionSpec>& text, const std::vector<DolSectionSpec>& data,
                              uint32_t bssAddress, uint32_t bssSize) {
    if (text.size() > 7 || data.size() > 11) {
        throw std::runtime_error("Too many DOL sections");
    }
    std::vector<uint8_t> dol(0x100, 0);
    auto add = [&](int slot, const DolSectionSpec& s) {
        Align(dol, 0x20);
        PutBE32(dol, slot * 4, static_cast<uint32_t>(dol.size()));
        PutBE32(dol, 0x48 + slot * 4, s.address);
        PutBE32(dol, 0x90 + slot * 4, static_cast<uint32_t>(s.data.size()));
        dol.insert(dol.end(), s.data.begin(), s.data.end());
    };
    for (size_t i = 0; i < text.size(); i++) {
        add(static_cast<int>(i), text[i]);
    }
    for (size_t i = 0; i < data.size(); i++) {
        add(static_cast<int>(7 + i), data[i]);
    }
    PutBE32(dol, 0xD8, bssAddress);
    PutBE32(dol, 0xDC, bssSize);
    PutBE32(dol, 0xE0, text.empty() ? 0 : text[0].address);
    return dol;
}

std::vector<uint8_t> BuildGcm(const std::string& gameId, uint8_t revision, const std::vector<uint8_t>& dol,
                              const std::map<std::string, std::vector<uint8_t>>& files) {
    struct Dir {
        std::map<std::string, Dir> dirs;
        std::map<std::string, const std::vector<uint8_t>*> files;
    };
    Dir root;
    for (const auto& [path, data] : files) {
        Dir* dir = &root;
        size_t start = 0;
        size_t slash;
        while ((slash = path.find('/', start)) != std::string::npos) {
            dir = &dir->dirs[path.substr(start, slash - start)];
            start = slash + 1;
        }
        dir->files[path.substr(start)] = &data;
    }

    struct Entry {
        bool dir;
        std::string name;
        uint32_t a;
        uint32_t b;
        const std::vector<uint8_t>* data;
    };
    std::vector<Entry> entries = { { true, "", 0, 0, nullptr } };
    std::function<void(const Dir&, uint32_t)> emit = [&](const Dir& dir, uint32_t parent) {
        for (const auto& [name, data] : dir.files) {
            entries.push_back({ false, name, 0, static_cast<uint32_t>(data->size()), data });
        }
        for (const auto& [name, sub] : dir.dirs) {
            const uint32_t index = static_cast<uint32_t>(entries.size());
            entries.push_back({ true, name, parent, 0, nullptr });
            emit(sub, index);
            entries[index].b = static_cast<uint32_t>(entries.size());
        }
    };
    emit(root, 0);
    entries[0].b = static_cast<uint32_t>(entries.size());

    std::vector<uint8_t> image(0x2500, 0);
    for (size_t i = 0; i < gameId.size() && i < 6; i++) {
        image[i] = gameId[i];
    }
    image[7] = revision;
    PutBE32(image, 0x1C, 0xC2339F3D);
    PutBE32(image, 0x420, static_cast<uint32_t>(image.size()));
    image.insert(image.end(), dol.begin(), dol.end());
    Align(image, 0x20);

    std::vector<uint8_t> fst(entries.size() * 12, 0);
    std::string strings;
    const uint32_t fstOffset = static_cast<uint32_t>(image.size());
    for (size_t i = 0; i < entries.size(); i++) {
        const uint32_t nameOffset = static_cast<uint32_t>(strings.size());
        if (i != 0) {
            strings += entries[i].name;
            strings.push_back('\0');
        }
        PutBE32(fst, i * 12, (entries[i].dir ? 0x01000000u : 0u) | nameOffset);
        PutBE32(fst, i * 12 + 4, entries[i].a);
        PutBE32(fst, i * 12 + 8, entries[i].b);
    }
    fst.insert(fst.end(), strings.begin(), strings.end());
    image.insert(image.end(), fst.begin(), fst.end());
    PutBE32(image, 0x424, fstOffset);
    PutBE32(image, 0x428, static_cast<uint32_t>(fst.size()));
    PutBE32(image, 0x42C, static_cast<uint32_t>(fst.size()));

    for (size_t i = 1; i < entries.size(); i++) {
        if (entries[i].dir) {
            continue;
        }
        Align(image, 0x8000);
        PutBE32(image, fstOffset + i * 12 + 4, static_cast<uint32_t>(image.size()));
        image.insert(image.end(), entries[i].data->begin(), entries[i].data->end());
    }
    Align(image, 0x8000);
    return image;
}

std::vector<uint8_t> BuildCiso(const std::vector<uint8_t>& image, uint32_t blockSize) {
    std::vector<uint8_t> out(0x8000, 0);
    out[0] = 'C', out[1] = 'I', out[2] = 'S', out[3] = 'O';
    out[4] = blockSize & 0xFF;
    out[5] = (blockSize >> 8) & 0xFF;
    out[6] = (blockSize >> 16) & 0xFF;
    out[7] = (blockSize >> 24) & 0xFF;
    const size_t blocks = (image.size() + blockSize - 1) / blockSize;
    for (size_t i = 0; i < blocks; i++) {
        const size_t start = i * blockSize;
        const size_t end = std::min(image.size(), start + blockSize);
        bool used = false;
        for (size_t j = start; j < end && !used; j++) {
            used = image[j] != 0;
        }
        if (!used) {
            continue;
        }
        out[8 + i] = 1;
        out.insert(out.end(), image.begin() + start, image.begin() + end);
        out.resize(out.size() + (blockSize - (end - start)), 0);
    }
    return out;
}

} // namespace Synthetic
