#!/usr/bin/env python3
"""Convert ZAPD/Shipwright XML asset definitions to Torch YAML format.

Reads XML asset definitions, converts them to Torch YAML, and adds VTX entries
discovered from the reference O2R manifest.

Usage:
    python3 zapd_to_torch.py <xml_dir> <dma_json> <out_dir> <manifest_json> <reference_o2r> [--types TYPE1,TYPE2,...]

Example:
    python3 soh/tools/zapd_to_torch.py \
        ~/code/claude/Shipwright/soh/assets/xml/GC_NMQ_PAL_F \
        soh/dma/pal_gc.json \
        soh/assets/yml/pal_gc \
        soh/manifests/pal_gc.json \
        soh/o2r/reference.o2r

    # Only convert specific types:
    python3 soh/tools/zapd_to_torch.py \
        ~/code/claude/Shipwright/soh/assets/xml/GC_NMQ_PAL_F \
        soh/dma/pal_gc.json \
        soh/assets/yml/pal_gc \
        soh/manifests/pal_gc.json \
        soh/o2r/reference.o2r \
        --types Texture,Blob,DList
"""

import argparse
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile


# Map XML element names to Torch YAML type strings
TYPE_MAP = {
    "Blob": "BLOB",
    "Texture": "TEXTURE",
    "DList": "GFX",
    "Vtx": "VTX",
    "Mtx": "MTX",
    "Array": "ARRAY",
    # OoT-specific types (Phase 2+)
    "Skeleton": "OOT:SKELETON",
    "Limb": "OOT:LIMB",
    "Animation": "OOT:ANIMATION",
    "LegacyAnimation": "OOT:ANIMATION",
    "CurveAnimation": "OOT:CURVE_ANIMATION",
    "PlayerAnimation": "OOT:PLAYER_ANIMATION",
    "PlayerAnimationData": "OOT:PLAYER_ANIMATION_DATA",
    "Scene": "OOT:SCENE",
    "Room": "OOT:ROOM",
    "Collision": "OOT:COLLISION",
    "Cutscene": "OOT:CUTSCENE",
    "Path": "OOT:PATH",
    "Text": "OOT:TEXT",
    "Soundfont": "OOT:SOUNDFONT",
    "Sample": "OOT:SAMPLE",
    "Sequence": "OOT:SEQUENCE",
    "Audio": "OOT:AUDIO",
}

# XML element names that are structural, not assets
SKIP_ELEMENTS = {"Root", "File", "ExternalFile", "Samples", "Sequences", "Symbol"}

# OTRExporter renames certain symbols for non-MQ ROMs (Main.cpp:165-171).
# Keys are the XML Name, values are the output symbol name.
NON_MQ_RENAMES = {
    "gTitleZeldaShieldLogoMQTex": "gTitleZeldaShieldLogoTex",
}

# Set from xml_dir in main(); True when the XML directory indicates a non-MQ ROM.
_is_non_mq = False


def convert_texture(elem):
    """Convert a Texture XML element to YAML dict."""
    entry = {
        "type": "TEXTURE",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
        "format": elem.get("Format").upper(),
        "width": int(elem.get("Width")),
        "height": int(elem.get("Height")),
    }
    if elem.get("TlutOffset"):
        entry["tlut"] = hex_val(elem.get("TlutOffset"))
    if elem.get("ExternalTlut"):
        entry["external_tlut"] = elem.get("ExternalTlut")
        entry["external_tlut_offset"] = hex_val(elem.get("ExternalTlutOffset"))
    return entry


def convert_blob(elem):
    """Convert a Blob XML element to YAML dict."""
    return {
        "type": "BLOB",
        "offset": hex_val(elem.get("Offset")),
        "size": hex_val(elem.get("Size")),
        "symbol": elem.get("Name"),
    }


def convert_limb_table(elem):
    """Convert a LimbTable XML element to a 0-byte BLOB.

    OTRExporter has no exporter for LimbTable, so it writes a 0-byte file.
    """
    return {
        "type": "BLOB",
        "offset": hex_val(elem.get("Offset")),
        "size": 0,
        "symbol": elem.get("Name"),
    }


def convert_dlist(elem):
    """Convert a DList XML element to YAML dict."""
    return {
        "type": "GFX",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
    }


def convert_vtx(elem):
    """Convert a Vtx XML element to YAML dict."""
    entry = {
        "type": "VTX",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
    }
    if elem.get("Count"):
        entry["count"] = int(elem.get("Count"))
    return entry


def convert_mtx(elem):
    """Convert a Mtx XML element to YAML dict."""
    return {
        "type": "MTX",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
    }


def convert_array(elem):
    """Convert an Array XML element to YAML dict."""
    entry = {
        "type": "OOT:ARRAY",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
        "count": int(elem.get("Count")),
    }
    # Check for child elements that define the array type
    for child in elem:
        if child.tag == "Vtx":
            entry["array_type"] = "VTX"
        elif child.tag == "Vector":
            vec_type = child.get("Type", "s16")
            dims = child.get("Dimensions", "3")
            if vec_type == "s16" and dims == "3":
                entry["array_type"] = "Vec3s"
            elif vec_type == "f32" and dims == "3":
                entry["array_type"] = "Vec3f"
    return entry


def convert_audio(elem):
    """Convert an Audio XML element with full metadata extraction."""
    entry = {
        "type": "OOT:AUDIO",
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
        "sound_font_table_offset": hex_val(elem.get("SoundFontTableOffset")),
        "sequence_table_offset": hex_val(elem.get("SequenceTableOffset")),
        "sample_bank_table_offset": hex_val(elem.get("SampleBankTableOffset")),
        "sequence_font_table_offset": hex_val(elem.get("SequenceFontTableOffset")),
    }

    # Extract sequence names
    seqs_elem = elem.find("Sequences")
    if seqs_elem is not None:
        entry["sequences"] = [s.get("Name") for s in seqs_elem.findall("Sequence")]

    # Extract sample names per bank
    samples_by_bank = []
    for samples_elem in elem.findall("Samples"):
        bank = int(samples_elem.get("Bank", "0"))
        bank_samples = []
        for sample in samples_elem.findall("Sample"):
            s = {"name": sample.get("Name"), "offset": hex_val(sample.get("Offset"))}
            if sample.get("SampleRate"):
                s["sample_rate"] = int(sample.get("SampleRate"))
            bank_samples.append(s)
        samples_by_bank.append({"bank": bank, "entries": bank_samples})
    if samples_by_bank:
        entry["samples"] = samples_by_bank

    return entry


def convert_generic(elem):
    """Generic converter for OoT-specific types (Phase 2+)."""
    torch_type = TYPE_MAP.get(elem.tag)
    if not torch_type:
        return None
    entry = {
        "type": torch_type,
        "offset": hex_val(elem.get("Offset")),
        "symbol": elem.get("Name"),
    }
    # Preserve type-specific attributes
    if elem.get("Size"):
        entry["size"] = hex_val(elem.get("Size"))
    if elem.get("LimbType"):
        entry["limb_type"] = elem.get("LimbType")
    if elem.get("Type"):
        entry["skel_type"] = elem.get("Type")
    if elem.get("FrameCount"):
        entry["frame_count"] = int(elem.get("FrameCount"))
    if elem.get("NumPaths"):
        entry["num_paths"] = int(elem.get("NumPaths"))
    if elem.get("SkelOffset"):
        entry["skel_offset"] = hex_val(elem.get("SkelOffset"))
    if elem.tag == "LegacyAnimation":
        entry["anim_type"] = "legacy"
    if elem.get("CodeOffset"):
        entry["code_offset"] = hex_val(elem.get("CodeOffset"))
    if elem.get("LangOffset"):
        entry["lang_offset"] = hex_val(elem.get("LangOffset"))
    return entry


def hex_val(v, default="0x0"):
    """Normalize a hex value: ensure 0x prefix, default if None."""
    if v is None:
        return default
    v = v.strip()
    if v.startswith("0x") or v.startswith("0X"):
        return v
    # Bare hex string from XML (e.g. "70" meaning 0x70)
    return "0x" + v


CONVERTERS = {
    "Texture": convert_texture,
    "Blob": convert_blob,
    "DList": convert_dlist,
    "Vtx": convert_vtx,
    "Mtx": convert_mtx,
    "Array": convert_array,
    "LimbTable": convert_limb_table,
    "Audio": convert_audio,
}


def yaml_value(v):
    """Format a value for YAML output."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        # Keep hex values as-is
        if v.startswith("0x") or v.startswith("0X"):
            return v
        return v
    return str(v)


def _format_config(segment, phys_start, extra_segments=None, external_files=None, virtual=None, directory=None):
    """Format the :config: section of a YAML file."""
    lines = [":config:\n", "  segments:\n", f"    - [ {segment}, {phys_start} ]\n"]
    if extra_segments:
        for seg_num, seg_start in extra_segments:
            lines.append(f"    - [ {seg_num}, {seg_start} ]\n")
    if virtual:
        lines.append(f"  virtual: [ {virtual[0]}, {virtual[1]} ]\n")
    if directory:
        lines.append(f"  directory: {directory}\n")
    if external_files:
        lines.append("  external_files:\n")
        for ef in external_files:
            lines.append(f"    - {ef}\n")
    lines.append("\n")
    return "".join(lines)


def _format_asset(asset):
    """Format a single asset entry as YAML text."""
    name = asset.get("symbol", asset.get("type", "unknown"))
    if _is_non_mq:
        name = NON_MQ_RENAMES.get(name, name)
        asset["symbol"] = name
    lines = [f"{name}:\n"]
    for k, v in asset.items():
        if isinstance(v, list):
            lines.append(f"  {k}:\n")
            for item in v:
                if isinstance(item, dict):
                    # Nested dict in list (e.g., sample bank entries)
                    first = True
                    for dk, dv in item.items():
                        prefix = "    - " if first else "      "
                        first = False
                        if isinstance(dv, list):
                            lines.append(f"{prefix}{dk}:\n")
                            for sub in dv:
                                if isinstance(sub, dict):
                                    sfirst = True
                                    for sk, sv in sub.items():
                                        sp = "        - " if sfirst else "          "
                                        sfirst = False
                                        lines.append(f"{sp}{sk}: {yaml_value(sv)}\n")
                                else:
                                    lines.append(f"        - {yaml_value(sub)}\n")
                        else:
                            lines.append(f"{prefix}{dk}: {yaml_value(dv)}\n")
                else:
                    lines.append(f"    - {yaml_value(item)}\n")
        else:
            lines.append(f"  {k}: {yaml_value(v)}\n")
    lines.append("\n")
    return "".join(lines)


def _parse_existing_yaml(path):
    """Parse an existing YAML file to extract config section and existing asset names."""
    with open(path) as f:
        content = f.read()

    # Find all top-level YAML keys (non-indented lines ending with ':')
    lines = content.split("\n")
    existing_assets = set()
    config_end = 0

    for i, line in enumerate(lines):
        if line and not line.startswith(" ") and not line.startswith("\t") and line.endswith(":"):
            if line == ":config:":
                continue
            if config_end == 0:
                # First non-config key marks end of config section
                config_end = sum(len(l) + 1 for l in lines[:i])
            existing_assets.add(line[:-1])

    if config_end == 0:
        config_end = len(content)

    config_text = content[:config_end]
    assets_text = content[config_end:]
    return config_text, assets_text, existing_assets


def _asset_sort_key(asset):
    """Sort key: OOT:SCENE/OOT:ROOM first, then everything else.
    This ensures scene factory processes rooms before GFX DLists,
    preventing VTX auto-discovery conflicts."""
    t = asset.get("type", "")
    if t in ("OOT:SCENE", "OOT:ROOM"):
        return (0, asset.get("symbol", ""))
    return (1, asset.get("symbol", ""))

def write_yaml(path, segment, phys_start, assets, extra_segments=None, external_files=None, virtual=None, directory=None):
    """Write a Torch YAML file, merging with existing content if the file exists."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    assets = sorted(assets, key=_asset_sort_key)

    new_config = _format_config(segment, phys_start, extra_segments, external_files, virtual, directory=directory)

    if os.path.exists(path):
        old_config, old_assets, existing_names = _parse_existing_yaml(path)

        # Use the config with more segments/external_files (longer = more complete)
        config = new_config if len(new_config) >= len(old_config) else old_config

        # Append only new assets
        new_asset_text = ""
        for asset in assets:
            name = asset.get("symbol", asset.get("type", "unknown"))
            if name not in existing_names:
                new_asset_text += _format_asset(asset)

        if not new_asset_text and config == old_config:
            return  # Nothing new to add and config unchanged

        with open(path, "w") as f:
            f.write(config)
            f.write(old_assets)
            if not old_assets.endswith("\n"):
                f.write("\n")
            if new_asset_text:
                f.write(new_asset_text)
    else:
        with open(path, "w") as f:
            f.write(new_config)
            for asset in assets:
                f.write(_format_asset(asset))


def get_segment_from_xml(xml_path):
    """Parse an XML file and return the segment number from the first File element."""
    try:
        tree = ET.parse(xml_path)
        for file_elem in tree.getroot().iter("File"):
            # No Segment or Segment="0" → use ZAPD default 0x80 (virtual addresses)
            # Segment="0" is a stale ZAPD workaround, removed upstream in zeldaret/oot#1459
            seg = file_elem.get("Segment")
            if seg is None or seg == "0":
                return 0x80
            return int(seg)
    except (ET.ParseError, FileNotFoundError, ValueError):
        return None
    return None


def xml_has_asset_types(xml_path, types=None):
    """Check if an XML file has any asset elements of the given types (or any asset if types is None)."""
    if types is None:
        types = {"Texture", "Blob", "DList", "PlayerAnimationData"}
    try:
        tree = ET.parse(xml_path)
        for file_elem in tree.getroot().iter("File"):
            for elem in file_elem:
                if elem.tag in types:
                    return True
    except (ET.ParseError, FileNotFoundError):
        pass
    return False


def get_scene_prefix(xml_rel_path):
    """Determine scene output prefix (scenes/shared or scenes/nonmq).

    Matches OTRExporter's GetPrefix() logic in DisplayListExporter.cpp:1054-1088.
    For non-MQ ROMs, dungeons with MQ variants go to scenes/nonmq/,
    everything else goes to scenes/shared/.
    """
    xml_basename = os.path.basename(xml_rel_path)  # e.g. "bdan.xml"

    # Regex matching dungeons that have unique MQ variants
    mq_dungeons = re.compile(
        r"^((ydan)|(ddan)|(bdan)|(Bmori1)|(HIDAN)|(MIZUsin)"
        r"|(jyasinzou)|(HAKAdan)|(HAKAdanCH)|(ice_doukutu)|(men)|(ganontika))\.xml$"
    )

    if "dungeons/" in xml_rel_path and mq_dungeons.match(xml_basename):
        return "scenes/nonmq"
    return "scenes/shared"


def get_output_category(xml_rel_path):
    """Map XML relative path to output category directory."""
    # xml_rel_path is like "objects/object_lightbox.xml" or "scenes/dungeons/bdan.xml"
    if xml_rel_path.startswith("scenes/"):
        # Scene files go to scenes/{shared,nonmq}/
        # e.g. scenes/dungeons/bdan.xml → scenes/nonmq
        return get_scene_prefix(xml_rel_path)
    return os.path.dirname(xml_rel_path)


def get_scene_directory(xml_rel_path):
    """Get the scene directory for a scene XML file.

    All assets from a scene XML (scene + rooms) output under the same directory.
    e.g. scenes/dungeons/bdan.xml → scenes/nonmq/bdan_scene
    """
    prefix = get_scene_prefix(xml_rel_path)
    stem = os.path.splitext(os.path.basename(xml_rel_path))[0]  # "bdan"
    return f"{prefix}/{stem}_scene"


def process_xml(xml_path, xml_rel_path, dma_table, out_dir, allowed_types, xml_dir=None):
    """Process a single XML file and write YAML output(s)."""
    try:
        tree = ET.parse(xml_path)
    except ET.ParseError as e:
        print(f"  SKIP (parse error): {xml_rel_path}: {e}", file=sys.stderr)
        return 0, 0

    root = tree.getroot()
    category = get_output_category(xml_rel_path)
    files_written = 0
    assets_written = 0

    # Collect ExternalFile references (children of Root, not File)
    extra_segments = []
    external_files = []
    out_prefix = os.path.basename(os.path.normpath(out_dir))
    for ext_elem in root.iter("ExternalFile"):
        ext_xml_path = ext_elem.get("XmlPath", "")
        ext_dma_name = os.path.splitext(os.path.basename(ext_xml_path))[0]
        if ext_dma_name in dma_table:
            ext_full_path = os.path.join(xml_dir, ext_xml_path) if xml_dir else None
            # Only include if the external XML has texture/DList assets we can use
            if ext_full_path and not xml_has_asset_types(ext_full_path):
                continue
            ext_seg = get_segment_from_xml(ext_full_path) if ext_full_path else None
            if ext_seg is not None:
                extra_segments.append((ext_seg, dma_table[ext_dma_name]["phys_start"]))
                ext_category = os.path.dirname(ext_xml_path)
                external_files.append(f"{out_prefix}/{ext_category}/{ext_dma_name}.yml")

    for file_elem in root.iter("File"):
        dma_name = file_elem.get("Name")
        out_name = file_elem.get("OutName", dma_name)
        # No Segment or Segment="0" → use ZAPD default 0x80 (virtual addresses)
        # Segment="0" is a stale ZAPD workaround, removed upstream in zeldaret/oot#1459
        seg_attr = file_elem.get("Segment")
        segment = 0x80 if seg_attr is None or seg_attr == "0" else int(seg_attr)
        base_address = file_elem.get("BaseAddress")

        if dma_name not in dma_table:
            print(f"  SKIP (no DMA entry): {dma_name}", file=sys.stderr)
            continue

        dma_entry = dma_table[dma_name]
        phys_start = dma_entry["phys_start"]

        # Copy per-XML externals and auto-add segments for objects with DLists
        file_extra_segments = list(extra_segments)
        file_external_files = list(external_files)
        has_dlists = any(elem.tag == "DList" for elem in file_elem)
        if has_dlists:
            # Auto-add gameplay_keep (segment 4)
            gk_name = "gameplay_keep"
            if gk_name in dma_table and dma_name != gk_name:
                gk_already = any(seg == 4 for seg, _ in file_extra_segments)
                if not gk_already:
                    file_extra_segments.append((4, dma_table[gk_name]["phys_start"]))
                    file_external_files.append(f"{out_prefix}/objects/{gk_name}.yml")
            # Auto-add segments 8-13 = same file (used for skeleton/limb texture references)
            # OoT uses these segments for eye textures, mouth textures, and limb DLists
            for extra_seg in range(8, 14):
                if not any(seg == extra_seg for seg, _ in file_extra_segments):
                    file_extra_segments.append((extra_seg, phys_start))

        # Auto-add audio segments when an Audio element is present
        has_audio = any(elem.tag == "Audio" for elem in file_elem)
        if has_audio:
            for seg_name, seg_id in [("Audiobank", 1), ("Audioseq", 2), ("Audiotable", 3)]:
                if seg_name in dma_table:
                    if not any(seg == seg_id for seg, _ in file_extra_segments):
                        file_extra_segments.append((seg_id, dma_table[seg_name]["phys_start"]))

        is_room_file = xml_rel_path.startswith("scenes/") and "_room_" in out_name

        assets = []
        for elem in file_elem:
            if elem.tag in SKIP_ELEMENTS:
                continue

            # Skip DList entries in room files that will be auto-discovered by the
            # scene factory's SetMesh processing. Keep others (child DLists, scene-level).
            # All DLists MUST be ordered after OOT:ROOM in the YAML (see write_yaml
            # sorting) to avoid VTX auto-discovery conflicts.
            # We can't tell which DLists are mesh vs child from XML alone, so we keep
            # all of them. The scene factory's AddAsset deduplicates: if it already
            # auto-discovered a DList at the same offset, the YAML entry is a no-op.
            # DLists at new offsets (child DLists, scene-level) get created normally.

            if allowed_types and elem.tag not in allowed_types:
                continue

            converter = CONVERTERS.get(elem.tag, convert_generic)
            entry = converter(elem)
            if entry:
                # Text assets need the code section's physical ROM address
                if elem.tag == "Text" and "code" in dma_table:
                    entry["code_phys_start"] = dma_table["code"]["phys_start"]
                assets.append(entry)

        if not assets:
            continue

        yaml_path = os.path.join(out_dir, category, f"{out_name}.yml")
        virtual = (hex_val(base_address), phys_start) if base_address else None

        # Directory overrides for assets that need custom output paths.
        directory = None

        # Audio: YAML goes to audio.yml (not audio/audio.yml) so the asset
        # path audio/audio matches the YAML-derived path (dirname=audio, symbol=audio).
        if has_audio:
            yaml_path = os.path.join(out_dir, f"{out_name}.yml")

        # For scene files, room YAMLs need a directory override so their assets
        # output under the scene's directory (e.g. scenes/nonmq/bdan_scene).
        if xml_rel_path.startswith("scenes/"):
            scene_dir = get_scene_directory(xml_rel_path)
            # Room files need the override; scene files get the right path from filename
            if not out_name.endswith("_scene"):
                directory = scene_dir
                # Room DLists reference segment 2 textures from the scene file.
                # Add the scene's segment 2 so Torch can resolve those references.
                stem = os.path.splitext(os.path.basename(xml_rel_path))[0]
                scene_dma_name = f"{stem}_scene"
                if scene_dma_name in dma_table:
                    scene_phys = dma_table[scene_dma_name]["phys_start"]
                    if not any(seg == 2 for seg, _ in file_extra_segments):
                        file_extra_segments.append((2, scene_phys))
                    # Add scene YAML as external file so Torch can find scene textures
                    scene_yml = f"{out_prefix}/{category}/{scene_dma_name}.yml"
                    if scene_yml not in file_external_files:
                        file_external_files.append(scene_yml)
                # Mesh type 2 (cullable) room DLists reference gMtxClear via VRAM address.
                # Add code/sys_matrix.yml so Torch can resolve the VRAM matrix reference.
                sys_matrix_yml = f"{out_prefix}/code/sys_matrix.yml"
                if sys_matrix_yml not in file_external_files:
                    file_external_files.append(sys_matrix_yml)

        write_yaml(yaml_path, segment, phys_start, assets,
                   extra_segments=file_extra_segments or None,
                   external_files=file_external_files or None,
                   virtual=virtual,
                   directory=directory)
        files_written += 1
        assets_written += len(assets)

    return files_written, assets_written


# --- VTX discovery from reference O2R ---

def extract_vtx_entries(manifest, reference_o2r):
    """Extract VTX entries from manifest and reference O2R.

    Returns dict: {dma_filename: [(asset_name, offset_hex, count), ...]}
    """
    vtx_pattern = re.compile(r"^(.+?)/(.+?)/(.*Vtx_([0-9A-Fa-f]+))$")

    vtx_by_file = {}
    vtx_assets = []

    for asset_path in manifest:
        m = vtx_pattern.match(asset_path)
        if not m:
            continue
        category, filename, asset_name, offset_hex = m.groups()
        vtx_assets.append((asset_path, category, filename, asset_name, int(offset_hex, 16)))

    # Extract counts from reference O2R
    with zipfile.ZipFile(reference_o2r, "r") as zf:
        for asset_path, category, filename, asset_name, offset in vtx_assets:
            try:
                data = zf.read(asset_path)
            except KeyError:
                print(f"  SKIP (not in O2R): {asset_path}", file=sys.stderr)
                continue

            # Parse: 64-byte header, then array_type (u32 LE), count (u32 LE)
            if len(data) < 72:
                print(f"  SKIP (too small): {asset_path}", file=sys.stderr)
                continue

            arr_type = struct.unpack_from("<I", data, 64)[0]
            count = struct.unpack_from("<I", data, 68)[0]

            if arr_type != 25:  # Not Vertex type
                print(f"  SKIP (not vertex array, type={arr_type}): {asset_path}", file=sys.stderr)
                continue

            key = f"{category}/{filename}"
            if key not in vtx_by_file:
                vtx_by_file[key] = []
            vtx_by_file[key].append((asset_name, f"0x{offset:X}", count))

    return vtx_by_file


def add_vtx_to_yaml(yaml_path, vtx_entries):
    """Append VTX entries to a YAML file."""
    with open(yaml_path) as f:
        content = f.read()

    # Check which entries already exist
    existing = set()
    for line in content.split("\n"):
        if line and not line.startswith(" ") and line.endswith(":") and line != ":config:":
            existing.add(line[:-1])

    new_entries = []
    for asset_name, offset_hex, count in sorted(vtx_entries, key=lambda x: int(x[1], 16)):
        if asset_name in existing:
            continue
        new_entries.append(
            f"{asset_name}:\n"
            f"  type: OOT:ARRAY\n"
            f"  offset: {offset_hex}\n"
            f"  symbol: {asset_name}\n"
            f"  count: {count}\n"
            f"  array_type: VTX\n"
        )

    if not new_entries:
        return 0

    # Ensure file ends with newline
    if not content.endswith("\n"):
        content += "\n"

    with open(yaml_path, "w") as f:
        f.write(content)
        for entry in new_entries:
            f.write("\n")
            f.write(entry)

    return len(new_entries)


def add_vtx_from_manifest(manifest_json, reference_o2r, yaml_dir):
    """Add VTX entries to YAML files based on reference O2R manifest."""
    with open(manifest_json) as f:
        manifest = json.load(f)

    vtx_by_file = extract_vtx_entries(manifest, reference_o2r)

    total_added = 0
    files_updated = 0

    for file_key, vtx_entries in sorted(vtx_by_file.items()):
        yaml_path = os.path.join(yaml_dir, f"{file_key}.yml")
        if not os.path.exists(yaml_path):
            continue

        added = add_vtx_to_yaml(yaml_path, vtx_entries)
        if added > 0:
            total_added += added
            files_updated += 1

    return total_added, files_updated


def main():
    parser = argparse.ArgumentParser(description="Convert ZAPD/Shipwright XML to Torch YAML")
    parser.add_argument("xml_dir", help="Path to XML directory (e.g. GC_NMQ_PAL_F)")
    parser.add_argument("dma_json", help="Path to DMA table JSON")
    parser.add_argument("out_dir", help="Output YAML directory")
    parser.add_argument("manifest_json", help="Path to reference manifest JSON")
    parser.add_argument("reference_o2r", help="Path to reference O2R file")
    parser.add_argument("--types", help="Comma-separated list of XML types to convert (default: all)")
    args = parser.parse_args()

    # Detect MQ status from xml_dir name (e.g. GC_NMQ_PAL_F vs GC_MQ_PAL_F)
    global _is_non_mq
    xml_dir_name = os.path.basename(os.path.normpath(args.xml_dir))
    _is_non_mq = "_NMQ_" in xml_dir_name or not "_MQ_" in xml_dir_name

    with open(args.dma_json) as f:
        dma_table = json.load(f)

    allowed_types = set(args.types.split(",")) if args.types else None

    # Auto-include dependency types:
    # - Array, Vtx, Mtx: dependencies of DLists
    # - Limb: dependencies of Skeletons
    # - PlayerAnimation: header entries that reference PlayerAnimationData
    if allowed_types:
        allowed_types.update({"Array", "Vtx", "Mtx", "Limb", "PlayerAnimation"})

    # Step 1: Convert XML to YAML
    total_files = 0
    total_assets = 0

    for dirpath, _, filenames in sorted(os.walk(args.xml_dir)):
        for fn in sorted(filenames):
            if not fn.endswith(".xml"):
                continue
            xml_path = os.path.join(dirpath, fn)
            xml_rel_path = os.path.relpath(xml_path, args.xml_dir)
            files, assets = process_xml(xml_path, xml_rel_path, dma_table, args.out_dir, allowed_types, xml_dir=args.xml_dir)
            total_files += files
            total_assets += assets

    print(f"Wrote {total_files} YAML files with {total_assets} assets")

    # Step 2: Add VTX entries from reference O2R
    vtx_added, vtx_files = add_vtx_from_manifest(args.manifest_json, args.reference_o2r, args.out_dir)
    print(f"Added {vtx_added} VTX entries to {vtx_files} YAML files")


if __name__ == "__main__":
    main()
