import fs from "node:fs";
import path from "node:path";

const [inputPath, outputPath, ...assignments] = process.argv.slice(2);
if (!inputPath || !outputPath || assignments.length === 0) {
  throw new Error("usage: node patch_esp_full_names.mjs input.esp output.esp EDID=Full Name [...]");
}

const replacements = new Map(assignments.map((assignment) => {
  const separator = assignment.indexOf("=");
  if (separator <= 0) throw new Error(`invalid assignment: ${assignment}`);
  return [assignment.slice(0, separator), assignment.slice(separator + 1)];
}));
const replaced = new Set();

function readType(buffer, offset) {
  return buffer.toString("ascii", offset, offset + 4);
}

function parseSubrecords(data) {
  const subrecords = [];
  for (let offset = 0; offset < data.length;) {
    if (offset + 6 > data.length) throw new Error(`truncated subrecord at ${offset}`);
    const type = readType(data, offset);
    let size = data.readUInt16LE(offset + 4);
    let headerSize = 6;
    if (type === "XXXX") {
      if (size !== 4 || offset + 16 > data.length) throw new Error(`invalid XXXX subrecord at ${offset}`);
      size = data.readUInt32LE(offset + 6);
      headerSize = 16;
    }
    const end = offset + headerSize + size;
    if (end > data.length) throw new Error(`subrecord ${type} exceeds record data`);
    subrecords.push({ type, data: data.subarray(offset + headerSize, end) });
    offset = end;
  }
  return subrecords;
}

function encodeSubrecord(type, data) {
  if (data.length > 0xffff) throw new Error(`subrecord ${type} is too large`);
  const output = Buffer.alloc(6 + data.length);
  output.write(type, 0, 4, "ascii");
  output.writeUInt16LE(data.length, 4);
  data.copy(output, 6);
  return output;
}

function patchRecord(record) {
  const flags = record.readUInt32LE(8);
  if ((flags & 0x00040000) !== 0) return record;
  const dataSize = record.readUInt32LE(4);
  const subrecords = parseSubrecords(record.subarray(24, 24 + dataSize));
  const editorId = subrecords.find((subrecord) => subrecord.type === "EDID")
    ?.data.toString("utf8").replace(/\0+$/, "");
  const replacement = editorId ? replacements.get(editorId) : undefined;
  if (replacement === undefined) return record;

  let foundFull = false;
  const patched = subrecords.map((subrecord) => {
    if (subrecord.type !== "FULL") return encodeSubrecord(subrecord.type, subrecord.data);
    foundFull = true;
    return encodeSubrecord("FULL", Buffer.from(`${replacement}\0`, "utf8"));
  });
  if (!foundFull) throw new Error(`record ${editorId} has no FULL subrecord`);

  const data = Buffer.concat(patched);
  const output = Buffer.concat([Buffer.from(record.subarray(0, 24)), data]);
  output.writeUInt32LE(data.length, 4);
  replaced.add(editorId);
  return output;
}

function patchElements(buffer, start = 0, end = buffer.length) {
  const elements = [];
  for (let offset = start; offset < end;) {
    if (offset + 24 > end) throw new Error(`truncated element at ${offset}`);
    const type = readType(buffer, offset);
    if (type === "GRUP") {
      const groupSize = buffer.readUInt32LE(offset + 4);
      if (groupSize < 24 || offset + groupSize > end) throw new Error(`invalid group at ${offset}`);
      const children = patchElements(buffer, offset + 24, offset + groupSize);
      const group = Buffer.concat([Buffer.from(buffer.subarray(offset, offset + 24)), children]);
      group.writeUInt32LE(group.length, 4);
      elements.push(group);
      offset += groupSize;
      continue;
    }

    const dataSize = buffer.readUInt32LE(offset + 4);
    const recordSize = 24 + dataSize;
    if (offset + recordSize > end) throw new Error(`record ${type} exceeds container`);
    elements.push(patchRecord(buffer.subarray(offset, offset + recordSize)));
    offset += recordSize;
  }
  return Buffer.concat(elements);
}

const output = patchElements(fs.readFileSync(inputPath));
for (const editorId of replacements.keys()) {
  if (!replaced.has(editorId)) throw new Error(`record not found: ${editorId}`);
}
fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, output);
console.log(`patched ${[...replaced].join(", ")} -> ${outputPath}`);
