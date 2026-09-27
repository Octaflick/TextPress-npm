import { access, copyFile, mkdir, rm } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const packageDirectory = resolve(scriptDirectory, "..");
const buildDirectory = resolve(packageDirectory, process.env.TEXTPRESS_WASM_BUILD_DIR ?? "build-wasm");
const distDirectory = resolve(packageDirectory, "dist");

const artifacts = [
  [resolve(buildDirectory, "textpress.js"), resolve(distDirectory, "textpress.js")],
  [resolve(buildDirectory, "textpress.wasm"), resolve(distDirectory, "textpress.wasm")],
  [resolve(packageDirectory, "index.js"), resolve(distDirectory, "index.js")],
  [resolve(packageDirectory, "index.d.ts"), resolve(distDirectory, "index.d.ts")]
];

for (const [source] of artifacts) {
  try {
    await access(source);
  } catch {
    throw new Error(`Missing build artifact: ${source}. Run \"npm run build:wasm\" first.`);
  }
}

await rm(distDirectory, { recursive: true, force: true });
await mkdir(distDirectory, { recursive: true });
await Promise.all(artifacts.map(([source, destination]) => copyFile(source, destination)));
