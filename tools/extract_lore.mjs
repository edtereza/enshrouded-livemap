// Usage: node tools/extract_lore.mjs <unpacked resources> <game folder> <pois.json> [language]
// Adds LoreReading entities without changing existing POI indices or discovery keys.
import fs from "node:fs/promises";
import path from "node:path";
import { gzipSync } from "node:zlib";

const [resources, game, output, language = "Pt_Br"] = process.argv.slice(2);
if (!resources || !game || !output) throw new Error("Expected resources, game folder and pois.json");
const read = async file => JSON.parse(await fs.readFile(file, "utf8"));
const files = async type => (await fs.readdir(path.join(resources, type))).filter(f => f.endsWith(".json"));
const collection = await read(path.join(resources, "LocaTagCollectionResource", (await files("LocaTagCollectionResource"))[0]));
const descriptor = collection.languages.find(l => l.language === language);
if (!descriptor) throw new Error("Language unavailable: " + language);
const hash = descriptor.data.hash;
const index = await fs.readFile(path.join(game, "enshrouded.kfc"));
const keysField = 16 + 8 * 7, valuesField = 16 + 8 * 8;
const keys = keysField + index.readUInt32LE(keysField);
const values = valuesField + index.readUInt32LE(valuesField);
let location = null;
for (let i = 0; i < index.readUInt32LE(keysField + 4); ++i) {
    const key = keys + i * 16;
    if (index.readUInt32LE(key) === hash.size && index.readUInt32LE(key + 4) === hash.hash0 &&
        index.readUInt32LE(key + 8) === hash.hash1 && index.readUInt32LE(key + 12) === hash.hash2) {
        const entry = values + i * 16;
        location = { offset: index.readUInt32LE(entry), container: index.readUInt16LE(entry + 6) };
        break;
    }
}
if (!location) throw new Error("Localization content missing");
const content = await fs.open(path.join(game, "enshrouded_" + String(location.container).padStart(3, "0") + ".dat"));
const blob = Buffer.alloc(hash.size);
try {
    const result = await content.read(blob, 0, blob.length, location.offset);
    if (result.bytesRead !== blob.length) throw new Error("Incomplete localization content");
} finally { await content.close(); }
const texts = new Map();
const base = blob.readInt32LE(0);
for (let i = 0; i < blob.readUInt32LE(4); ++i) {
    const record = base + i * 24;
    const start = record + 4 + blob.readInt32LE(record + 4);
    texts.set(blob.readUInt32LE(record), blob.subarray(start, start + blob.readUInt32LE(record + 8))
        .toString("utf8").replace(/<\/?[A-Za-z][A-Za-z0-9_]*>/g, "").trim());
}
function guidId(guid) {
    const bytes = Buffer.from(guid.replaceAll("-", ""), "hex");
    bytes.subarray(0, 4).reverse();
    bytes.subarray(4, 6).reverse();
    bytes.subarray(6, 8).reverse();
    let value = 0x811c9dc5;
    for (const byte of bytes) value = Math.imul(value ^ byte, 0x01000193) >>> 0;
    return value;
}
const text = guid => guid ? texts.get(guidId(guid)) || "" : "";
const templates = new Map();
for (const file of await files("TemplateResource")) {
    const template = await read(path.join(resources, "TemplateResource", file));
    const lore = template.components?.find(c => c.$type === "keen::ecs::LoreReading")?.$value;
    if (lore) templates.set(template.$guid || file.slice(0, 36), {
        title: text(lore.entryName) || "Bilhete",
        description: text(lore.description)
    });
}
const data = await read(output);
if (data.categories.some(c => c.id === "lore")) throw new Error("Lore is already present in this POI file");
const category = data.categories.length;
data.categories.push({ id: "lore", label: "Bilhetes e lore", color: "#e6c88e", on: true, label_default: "Bilhete" });
data.loreTexts = {};
const seen = new Set();
let count = 0;
for (const file of await files("SceneEntityChunkResource")) {
    const chunk = await read(path.join(resources, "SceneEntityChunkResource", file));
    for (const entity of chunk.entities || []) {
        const note = templates.get(chunk.templates?.[entity.templateIndex]);
        if (!note) continue;
        const transform = entity.transform;
        const fixed = axis => transform["position" + axis + "1"] + transform["position" + axis + "0"] / 65536;
        const [x, y, z] = ["X", "Y", "Z"].map(fixed);
        if (![x, y, z].every(Number.isFinite) || x < 0 || z < 0 || x > 10240 || z > 10240) continue;
        if ((x < 1600 && z > 8000) || (x < 2600 && z > 8200)) continue;
        const key = JSON.stringify([note.title, x, y, z]);
        if (seen.has(key)) continue;
        seen.add(key);
        data.loreTexts[data.pois.length] = note.description;
        data.pois.push([category, ...[x, y, z].map(v => Math.round(v * 10) / 10), note.title]);
        ++count;
    }
}
if (!count) throw new Error("No placed lore entities found");
const json = JSON.stringify(data);
await fs.writeFile(output, json);
await fs.writeFile(output + ".gz", gzipSync(json, { level: 9 }));
console.log(JSON.stringify({ templates: templates.size, placed: count, language }));
