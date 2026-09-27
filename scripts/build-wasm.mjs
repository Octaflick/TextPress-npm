import { mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const packageDirectory = resolve(scriptDirectory, "..");
const buildDirectory = resolve(packageDirectory, process.env.TEXTPRESS_WASM_BUILD_DIR ?? "build-wasm");
const compiler = process.env.EMXX ?? "em++";

await mkdir(buildDirectory, { recursive: true });

const arguments_ = [
  "-std=c++17",
  "-O2",
  "-fexceptions",
  `-I${resolve(packageDirectory, "cpp/Algorithms")}`,
  `-I${resolve(packageDirectory, "cpp/Bindings")}`,
  resolve(packageDirectory, "cpp/Bindings/textpress_wasm.cpp"),
  resolve(packageDirectory, "cpp/Algorithms/compressor.cpp"),
  resolve(packageDirectory, "cpp/Algorithms/LZ78.cpp"),
  resolve(packageDirectory, "cpp/Algorithms/huffman.cpp"),
  resolve(packageDirectory, "cpp/Algorithms/huffman_lz78.cpp"),
  "--no-entry",
  "-sMODULARIZE=1",
  "-sENVIRONMENT=web,worker,node",
  "-sALLOW_MEMORY_GROWTH=1",
  "-sFORCE_FILESYSTEM=1",
  "-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\",\"_textpress_version\",\"_textpress_last_error\",\"_textpress_compress\",\"_textpress_decompress\",\"_textpress_free_buffer\"]",
  "-sEXPORTED_RUNTIME_METHODS=[\"HEAPU8\",\"HEAPU32\"]",
  "-o",
  resolve(buildDirectory, "textpress.js")
];

const exitCode = await new Promise((resolveExit, reject) => {
  const child = spawn(compiler, arguments_, { cwd: packageDirectory, stdio: "inherit" });
  child.once("error", error => {
    if (error.code === "ENOENT") {
      reject(new Error("Could not find em++. Activate Emscripten first (for example: source ./emsdk/emsdk_env.sh)."));
      return;
    }
    reject(error);
  });
  child.once("exit", code => resolveExit(code ?? 1));
});

if (exitCode !== 0) {
  throw new Error(`Emscripten build failed with exit code ${exitCode}.`);
}
