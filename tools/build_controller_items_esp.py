#!/usr/bin/env python3
"""Add the direct Studio and Quick Outfit controller items to OutfitManager.esp."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24
PLUGIN_INDEX = 0x01


def form_id(local_id: int) -> int:
    return (PLUGIN_INDEX << 24) | local_id


@dataclass(frozen=True)
class Variant:
    effect_local_id: int
    item_local_id: int
    effect_editor_id: str
    effect_name: str
    script_name: str
    item_editor_id: str
    item_name: str


def read_u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def record_signature(record: bytes) -> bytes:
    return record[:4]


def record_form_id(record: bytes) -> int:
    return read_u32(record, 12)


def record_data(record: bytes) -> bytes:
    size = read_u32(record, 4)
    return record[RECORD_HEADER_SIZE : RECORD_HEADER_SIZE + size]


def rebuild_record(record: bytes, data: bytes, new_form_id: int | None = None) -> bytes:
    header = bytearray(record[:RECORD_HEADER_SIZE])
    struct.pack_into("<I", header, 4, len(data))
    if new_form_id is not None:
        struct.pack_into("<I", header, 12, new_form_id)
    return bytes(header) + data


def split_subrecords(data: bytes) -> list[tuple[bytes, bytes]]:
    result: list[tuple[bytes, bytes]] = []
    position = 0
    extended_size: int | None = None
    while position < len(data):
        if position + 6 > len(data):
            raise ValueError(f"truncated subrecord at {position:#x}")
        signature = data[position : position + 4]
        size = struct.unpack_from("<H", data, position + 4)[0]
        position += 6
        if signature == b"XXXX":
            if size != 4 or position + 4 > len(data):
                raise ValueError("invalid XXXX subrecord")
            extended_size = read_u32(data, position)
            position += 4
            continue
        actual_size = extended_size if extended_size is not None else size
        extended_size = None
        if position + actual_size > len(data):
            raise ValueError(f"subrecord {signature!r} exceeds record data")
        result.append((signature, data[position : position + actual_size]))
        position += actual_size
    return result


def join_subrecords(subrecords: list[tuple[bytes, bytes]]) -> bytes:
    output = bytearray()
    for signature, payload in subrecords:
        if len(payload) > 0xFFFF:
            output += b"XXXX" + struct.pack("<H", 4) + struct.pack("<I", len(payload))
            output += signature + struct.pack("<H", 0)
        else:
            output += signature + struct.pack("<H", len(payload))
        output += payload
    return bytes(output)


def replace_subrecord(record: bytes, signature: bytes, payload: bytes) -> bytes:
    subrecords = split_subrecords(record_data(record))
    replaced = 0
    for index, (current_signature, _) in enumerate(subrecords):
        if current_signature == signature:
            subrecords[index] = (current_signature, payload)
            replaced += 1
    if replaced != 1:
        raise ValueError(
            f"expected one {signature.decode(errors='replace')} in {record_signature(record)!r}, found {replaced}"
        )
    return rebuild_record(record, join_subrecords(subrecords))


def get_subrecord(record: bytes, signature: bytes) -> bytes:
    matches = [payload for current, payload in split_subrecords(record_data(record)) if current == signature]
    if len(matches) != 1:
        raise ValueError(f"expected one {signature!r}, found {len(matches)}")
    return matches[0]


def text_payload(text: str) -> bytes:
    return text.encode("utf-8") + b"\x00"


def clone_effect(source: bytes, variant: Variant) -> bytes:
    record = rebuild_record(source, record_data(source), form_id(variant.effect_local_id))
    record = replace_subrecord(record, b"EDID", text_payload(variant.effect_editor_id))
    record = replace_subrecord(record, b"FULL", text_payload(variant.effect_name))
    vmad = get_subrecord(record, b"VMAD")
    old_script = b"OMControllerEffectScript"
    new_script = variant.script_name.encode("ascii")
    if len(new_script) != len(old_script):
        raise ValueError(f"script name must be {len(old_script)} bytes: {variant.script_name}")
    if vmad.count(old_script) != 1:
        raise ValueError("controller script name was not found exactly once in VMAD")
    vmad = vmad.replace(old_script, new_script)
    old_item = struct.pack("<I", form_id(0x801))
    new_item = struct.pack("<I", form_id(variant.item_local_id))
    if vmad.count(old_item) != 1:
        raise ValueError("ControllerItem property was not found exactly once in VMAD")
    vmad = vmad.replace(old_item, new_item)
    return replace_subrecord(record, b"VMAD", vmad)


def clone_item(source: bytes, variant: Variant) -> bytes:
    record = rebuild_record(source, record_data(source), form_id(variant.item_local_id))
    record = replace_subrecord(record, b"EDID", text_payload(variant.item_editor_id))
    record = replace_subrecord(record, b"FULL", text_payload(variant.item_name))
    return replace_subrecord(record, b"EFID", struct.pack("<I", form_id(variant.effect_local_id)))


def patch_tes4_record(record: bytes, added_records: int, next_object_id: int) -> bytes:
    subrecords = split_subrecords(record_data(record))
    for index, (signature, payload) in enumerate(subrecords):
        if signature != b"HEDR":
            continue
        if len(payload) != 12:
            raise ValueError("unexpected HEDR size")
        hedr = bytearray(payload)
        struct.pack_into("<I", hedr, 4, read_u32(payload, 4) + added_records)
        struct.pack_into("<I", hedr, 8, max(read_u32(payload, 8), next_object_id))
        subrecords[index] = (signature, bytes(hedr))
        return rebuild_record(record, join_subrecords(subrecords))
    raise ValueError("TES4 HEDR was not found")


def parse_group_records(group: bytes) -> list[bytes]:
    records: list[bytes] = []
    position = GROUP_HEADER_SIZE
    while position < len(group):
        if position + RECORD_HEADER_SIZE > len(group):
            raise ValueError("truncated group record")
        if group[position : position + 4] == b"GRUP":
            raise ValueError("nested groups are not expected in OutfitManager.esp")
        size = read_u32(group, position + 4)
        end = position + RECORD_HEADER_SIZE + size
        if end > len(group):
            raise ValueError("record exceeds group")
        records.append(group[position:end])
        position = end
    return records


def rebuild_group(group: bytes, records: list[bytes]) -> bytes:
    header = bytearray(group[:GROUP_HEADER_SIZE])
    body = b"".join(records)
    struct.pack_into("<I", header, 4, GROUP_HEADER_SIZE + len(body))
    return bytes(header) + body


def patch_plugin(source: bytes, language: str) -> bytes:
    if source[:4] != b"TES4":
        raise ValueError("not a Fallout 4 plugin")
    tes4_end = RECORD_HEADER_SIZE + read_u32(source, 4)
    tes4 = source[:tes4_end]

    if language == "chs":
        main_name = "Aozora 青空服装管理"
        variants = [
            Variant(0x806, 0x807, "OM_StudioEffect", "打开服装工作台", "OMStudioCtrlEffectScript", "OM_StudioControllerItem", "Aozora 青空服装工作台"),
            Variant(0x808, 0x809, "OM_QuickOutfitEffect", "打开快速换装", "OMFastOutfitEffectScript", "OM_QuickOutfitControllerItem", "Aozora 青空快速换装"),
        ]
    else:
        main_name = "Aozora Outfit Manager"
        variants = [
            Variant(0x806, 0x807, "OM_StudioEffect", "Open Outfit Studio", "OMStudioCtrlEffectScript", "OM_StudioControllerItem", "Aozora Outfit Studio"),
            Variant(0x808, 0x809, "OM_QuickOutfitEffect", "Open Quick Outfit", "OMFastOutfitEffectScript", "OM_QuickOutfitControllerItem", "Aozora Quick Outfit"),
        ]

    groups: list[bytes] = []
    position = tes4_end
    source_effect: bytes | None = None
    source_item: bytes | None = None
    while position < len(source):
        if source[position : position + 4] != b"GRUP":
            raise ValueError(f"expected GRUP at {position:#x}")
        size = read_u32(source, position + 4)
        end = position + size
        if end > len(source):
            raise ValueError("group exceeds plugin")
        group = source[position:end]
        label = group[8:12]
        records = parse_group_records(group)
        if label == b"MGEF":
            source_effect = next((record for record in records if record_form_id(record) == form_id(0x802)), None)
            if source_effect is None:
                raise ValueError("source controller MGEF 0x802 not found")
            records.extend(clone_effect(source_effect, variant) for variant in variants)
            group = rebuild_group(group, records)
        elif label == b"ALCH":
            source_item = next((record for record in records if record_form_id(record) == form_id(0x801)), None)
            if source_item is None:
                raise ValueError("source controller ALCH 0x801 not found")
            records = [
                replace_subrecord(record, b"FULL", text_payload(main_name))
                if record_form_id(record) == form_id(0x801)
                else record
                for record in records
            ]
            records.extend(clone_item(source_item, variant) for variant in variants)
            group = rebuild_group(group, records)
        groups.append(group)
        position = end

    if source_effect is None or source_item is None:
        raise ValueError("controller source records were not found")
    tes4 = patch_tes4_record(tes4, added_records=4, next_object_id=0x80A)
    return tes4 + b"".join(groups)


def validate_plugin(data: bytes) -> None:
    tes4_end = RECORD_HEADER_SIZE + read_u32(data, 4)
    hedr = get_subrecord(data[:tes4_end], b"HEDR")
    if read_u32(hedr, 4) < 10 or read_u32(hedr, 8) < 0x80A:
        raise ValueError("TES4 record count or next object ID was not updated")
    found: dict[int, bytes] = {}
    position = tes4_end
    while position < len(data):
        size = read_u32(data, position + 4)
        group = data[position : position + size]
        for record in parse_group_records(group):
            found[record_form_id(record)] = record
        position += size
    required = [form_id(local) for local in (0x800, 0x801, 0x802, 0x803, 0x804, 0x805, 0x806, 0x807, 0x808, 0x809)]
    missing = [f"{record_id:08X}" for record_id in required if record_id not in found]
    if missing:
        raise ValueError(f"missing records: {', '.join(missing)}")
    if get_subrecord(found[form_id(0x807)], b"EFID") != struct.pack("<I", form_id(0x806)):
        raise ValueError("Studio item does not reference Studio effect")
    if get_subrecord(found[form_id(0x809)], b"EFID") != struct.pack("<I", form_id(0x808)):
        raise ValueError("Quick item does not reference Quick effect")
    studio_vmad = get_subrecord(found[form_id(0x806)], b"VMAD")
    if b"OMStudioCtrlEffectScript" not in studio_vmad or struct.pack("<I", form_id(0x807)) not in studio_vmad:
        raise ValueError("Studio effect VMAD does not reference its script and controller item")
    quick_vmad = get_subrecord(found[form_id(0x808)], b"VMAD")
    if b"OMFastOutfitEffectScript" not in quick_vmad or struct.pack("<I", form_id(0x809)) not in quick_vmad:
        raise ValueError("Quick effect VMAD does not reference its script and controller item")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--language", choices=("en", "chs"), required=True)
    args = parser.parse_args()
    source = args.source.read_bytes()
    output = patch_plugin(source, args.language)
    validate_plugin(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {args.output} ({len(source)} -> {len(output)} bytes)")


if __name__ == "__main__":
    main()
