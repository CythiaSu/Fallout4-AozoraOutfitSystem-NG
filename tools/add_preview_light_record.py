#!/usr/bin/env python3
"""Add the private long-range preview light record to OutfitManager.esp."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24
PLUGIN_INDEX = 0x01
PREVIEW_LIGHT_ID = 0x80A
SOURCE_LIGHT_ID = 0x000D8556


def form_id(local_id: int) -> int:
    return (PLUGIN_INDEX << 24) | local_id


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


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
            raise ValueError("nested groups are not expected in the LIGH group")
        size = read_u32(group, position + 4)
        end = position + RECORD_HEADER_SIZE + size
        if end > len(group):
            raise ValueError("record exceeds group")
        records.append(group[position:end])
        position = end
    return records


def find_light_group(data: bytes) -> tuple[int, bytes]:
    tes4_end = RECORD_HEADER_SIZE + read_u32(data, 4)
    position = tes4_end
    while position < len(data):
        if data[position : position + 4] != b"GRUP":
            raise ValueError(f"expected GRUP at {position:#x}")
        size = read_u32(data, position + 4)
        end = position + size
        group = data[position:end]
        if group[8:12] == b"LIGH" and read_u32(group, 12) == 0:
            return position, group
        position = end
    raise ValueError("Fallout4.esm LIGH group not found")


def find_source_light(data: bytes) -> bytes:
    _, group = find_light_group(data)
    for record in parse_group_records(group):
        if record_form_id(record) == SOURCE_LIGHT_ID:
            return record
    raise ValueError("Fallout4.esm light 00239222 not found")


def patch_tes4(record: bytes) -> bytes:
    subrecords = split_subrecords(record_data(record))
    for index, (signature, payload) in enumerate(subrecords):
        if signature != b"HEDR":
            continue
        if len(payload) != 12:
            raise ValueError("unexpected HEDR size")
        hedr = bytearray(payload)
        struct.pack_into("<I", hedr, 4, read_u32(payload, 4) + 1)
        struct.pack_into("<I", hedr, 8, max(read_u32(payload, 8), PREVIEW_LIGHT_ID + 1))
        subrecords[index] = (signature, bytes(hedr))
        return rebuild_record(record, join_subrecords(subrecords))
    raise ValueError("TES4 HEDR was not found")


def make_preview_light(source: bytes) -> bytes:
    record = rebuild_record(source, record_data(source), form_id(PREVIEW_LIGHT_ID))
    record = replace_subrecord(record, b"EDID", b"OM_PreviewLight\x00")
    data = bytearray(get_subrecord(record, b"DATA"))
    if len(data) < 20:
        raise ValueError("unexpected LIGH DATA size")
    struct.pack_into("<I", data, 4, 256)
    return replace_subrecord(record, b"DATA", bytes(data))


def patch_plugin(source: bytes, light_source: bytes) -> bytes:
    tes4_end = RECORD_HEADER_SIZE + read_u32(source, 4)
    position = tes4_end
    while position < len(source):
        size = read_u32(source, position + 4)
        group = source[position : position + size]
        if group[8:12] == b"LIGH":
            if any(record_form_id(record) == form_id(PREVIEW_LIGHT_ID) for record in parse_group_records(group)):
                raise ValueError("preview light record already exists")
        position += size
    tes4 = patch_tes4(source[:tes4_end])
    _, source_group = find_light_group(light_source)
    group_header = bytearray(source_group[:GROUP_HEADER_SIZE])
    preview_light = make_preview_light(find_source_light(light_source))
    struct.pack_into("<I", group_header, 4, GROUP_HEADER_SIZE + len(preview_light))
    return tes4 + bytes(group_header) + preview_light + source[tes4_end:]


def validate(data: bytes) -> None:
    tes4_end = RECORD_HEADER_SIZE + read_u32(data, 4)
    hedr = get_subrecord(data[:tes4_end], b"HEDR")
    if read_u32(hedr, 4) < 11 or read_u32(hedr, 8) < PREVIEW_LIGHT_ID + 1:
        raise ValueError("TES4 HEDR was not updated")
    position = tes4_end
    found = None
    while position < len(data):
        size = read_u32(data, position + 4)
        group = data[position : position + size]
        if group[8:12] == b"LIGH":
            for record in parse_group_records(group):
                if record_form_id(record) == form_id(PREVIEW_LIGHT_ID):
                    found = record
        position += size
    if found is None:
        raise ValueError("preview light record was not added")
    edid = get_subrecord(found, b"EDID")
    data_subrecord = get_subrecord(found, b"DATA")
    if edid != b"OM_PreviewLight\x00" or read_u32(data_subrecord, 4) != 256:
        raise ValueError("preview light record has unexpected values")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("fallout4_esm", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_bytes()
    light_source = args.fallout4_esm.read_bytes()
    output = patch_plugin(source, light_source)
    validate(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {args.output} ({len(source)} -> {len(output)} bytes)")


if __name__ == "__main__":
    main()
