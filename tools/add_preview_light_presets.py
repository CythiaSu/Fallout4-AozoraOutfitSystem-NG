#!/usr/bin/env python3
"""Clone the stable OutfitManager preview LIGH into fixed fade presets."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24
BASE_LIGHT_ID = 0x80A
LAST_LIGHT_ID = 0x823
PLUGIN_INDEX = 0x01

PRESETS = (
    (0x814, "OM_PreviewLight_Exterior_25", 0.125),
    (0x815, "OM_PreviewLight_Exterior_50", 0.250),
    (0x816, "OM_PreviewLight_Exterior_75", 0.375),
    (0x817, "OM_PreviewLight_Exterior_100", 0.500),
    (0x818, "OM_PreviewLight_Exterior_125", 0.625),
    (0x819, "OM_PreviewLight_Exterior_150", 0.750),
    (0x81A, "OM_PreviewLight_Exterior_175", 0.875),
    (0x81B, "OM_PreviewLight_Exterior_200", 1.000),
    (0x81C, "OM_PreviewLight_Interior_25", 0.500),
    (0x81D, "OM_PreviewLight_Interior_50", 0.750),
    (0x81E, "OM_PreviewLight_Interior_75", 1.000),
    (0x81F, "OM_PreviewLight_Interior_125", 2.500),
    (0x820, "OM_PreviewLight_Interior_150", 2.750),
    (0x821, "OM_PreviewLight_Interior_175", 3.000),
    (0x822, "OM_PreviewLight_Interior_200", 3.250),
    (0x823, "OM_PreviewLight_Interior_100", 2.250),
)


def read_u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def form_id(local_id: int) -> int:
    return (PLUGIN_INDEX << 24) | local_id


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
        size = read_u16(data, position + 4)
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


def get_subrecord(record: bytes, signature: bytes) -> bytes:
    matches = [payload for current, payload in split_subrecords(record_data(record)) if current == signature]
    if len(matches) != 1:
        raise ValueError(f"expected one {signature!r}, found {len(matches)}")
    return matches[0]


def replace_subrecord(record: bytes, signature: bytes, payload: bytes) -> bytes:
    subrecords = split_subrecords(record_data(record))
    replaced = 0
    for index, (current_signature, _) in enumerate(subrecords):
        if current_signature == signature:
            subrecords[index] = (current_signature, payload)
            replaced += 1
    if replaced != 1:
        raise ValueError(f"expected one {signature!r}, found {replaced}")
    return rebuild_record(record, join_subrecords(subrecords))


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


def patch_tes4(record: bytes) -> bytes:
    subrecords = split_subrecords(record_data(record))
    for index, (signature, payload) in enumerate(subrecords):
        if signature != b"HEDR":
            continue
        if len(payload) != 12:
            raise ValueError("unexpected HEDR size")
        hedr = bytearray(payload)
        struct.pack_into("<I", hedr, 4, read_u32(payload, 4) + len(PRESETS))
        struct.pack_into("<I", hedr, 8, max(read_u32(payload, 8), LAST_LIGHT_ID + 1))
        subrecords[index] = (signature, bytes(hedr))
        return rebuild_record(record, join_subrecords(subrecords))
    raise ValueError("TES4 HEDR was not found")


def make_preset(base: bytes, local_id: int, editor_id: str, fade: float) -> bytes:
    record = rebuild_record(base, record_data(base), form_id(local_id))
    record = replace_subrecord(record, b"EDID", editor_id.encode("ascii") + b"\x00")
    return replace_subrecord(record, b"FNAM", struct.pack("<f", fade))


def patch_plugin(source: bytes) -> bytes:
    tes4_end = RECORD_HEADER_SIZE + read_u32(source, 4)
    groups: list[bytes] = []
    position = tes4_end
    base: bytes | None = None
    ligh_group_index: int | None = None

    while position < len(source):
        if source[position : position + 4] != b"GRUP":
            raise ValueError(f"expected GRUP at {position:#x}")
        size = read_u32(source, position + 4)
        end = position + size
        if end > len(source):
            raise ValueError("group exceeds plugin")
        group = source[position:end]
        if group[8:12] == b"LIGH" and read_u32(group, 12) == 0:
            ligh_group_index = len(groups)
            records = parse_group_records(group)
            for record in records:
                if record_form_id(record) == form_id(BASE_LIGHT_ID):
                    base = record
            existing_ids = {record_form_id(record) for record in records}
            required_ids = {form_id(local_id) for local_id, _, _ in PRESETS}
            if existing_ids & required_ids:
                raise ValueError("one or more preview preset records already exist")
            groups.append(group)
        else:
            groups.append(group)
        position = end

    if base is None or ligh_group_index is None:
        raise ValueError("stable OM_PreviewLight record or LIGH group was not found")

    records = parse_group_records(groups[ligh_group_index])
    records.extend(make_preset(base, local_id, editor_id, fade) for local_id, editor_id, fade in PRESETS)
    groups[ligh_group_index] = rebuild_group(groups[ligh_group_index], records)
    return patch_tes4(source[:tes4_end]) + b"".join(groups)


def validate(data: bytes) -> None:
    tes4_end = RECORD_HEADER_SIZE + read_u32(data, 4)
    hedr = get_subrecord(data[:tes4_end], b"HEDR")
    if read_u32(hedr, 4) < 15 + len(PRESETS) or read_u32(hedr, 8) < LAST_LIGHT_ID + 1:
        raise ValueError("TES4 HEDR was not updated for the 0.25 preview light presets")

    found: dict[int, bytes] = {}
    position = tes4_end
    while position < len(data):
        size = read_u32(data, position + 4)
        group = data[position : position + size]
        if group[8:12] == b"LIGH":
            for record in parse_group_records(group):
                found[record_form_id(record)] = record
        position += size

    base = found.get(form_id(BASE_LIGHT_ID))
    if base is None:
        raise ValueError("base preview light record is missing")
    base_data = record_data(base)
    for local_id, editor_id, fade in PRESETS:
        record = found.get(form_id(local_id))
        if record is None:
            raise ValueError(f"preset record {local_id:#x} is missing")
        if get_subrecord(record, b"EDID") != editor_id.encode("ascii") + b"\x00":
            raise ValueError(f"preset record {local_id:#x} has an unexpected EDID")
        if record_data(record) == base_data:
            raise ValueError(f"preset record {local_id:#x} did not change FNAM")
        if get_subrecord(record, b"DATA") != get_subrecord(base, b"DATA"):
            raise ValueError(f"preset record {local_id:#x} changed DATA parameters")
        actual_fade = struct.unpack("<f", get_subrecord(record, b"FNAM"))[0]
        if abs(actual_fade - fade) > 0.0001:
            raise ValueError(f"preset record {local_id:#x} has an unexpected fade")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = patch_plugin(args.source.read_bytes())
    validate(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {args.output} ({args.source.stat().st_size} -> {len(output)} bytes)")


if __name__ == "__main__":
    main()
