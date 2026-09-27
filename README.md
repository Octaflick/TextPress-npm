# TextPress

A **CLI-based file compression tool** built with C++17, supporting Huffman coding and LZ78 dictionary compression. The project builds natively and with Emscripten for WebAssembly.

---

## Quick Start

```bash
# Configure and build the native C++ executable.
cmake -S . -B build
cmake --build build

# Compress a file using Huffman coding.
./build/zipcompress compress input.txt output.huff

# Compress a file using LZ78.
./build/zipcompress compress input.txt output.lz78

# Compress with the Huffman-LZ78 hybrid, then decompress.
./build/zipcompress compress input.txt output.hlz
./build/zipcompress decompress output.hlz restored.txt

# Show help.
./build/zipcompress help
```

### WebAssembly

The WASM build exports a small C ABI and wraps it in a typed ES-module API. It works in browsers, web workers, and Node.js; compression data is passed directly as `Uint8Array` values, so the JavaScript API does not depend on Emscripten's virtual filesystem.

Build the npm-ready artifact with an activated Emscripten SDK:

```bash
# Only needed when Emscripten is not already on PATH.
source ./emsdk/emsdk_env.sh

npm run build
npm test
```

For CMake-based integrations, the equivalent target is `textpress_wasm`:

```bash
emcmake cmake -S . -B build-wasm -DTEXTPRESS_BUILD_CLI=OFF
cmake --build build-wasm --target textpress_wasm
```

This produces the publishable files under `dist/`:

- `textpress.mjs` and `textpress.wasm` — generated Emscripten module
- `index.js` — stable JavaScript API
- `index.d.ts` — TypeScript declarations

Use the package API after publishing or from the generated `dist/` directory:

```js
import { createTextPress } from "@textpress/wasm";

const textpress = await createTextPress();
const source = new TextEncoder().encode("Compress me");

const compressed = textpress.compress(source, ".hlz");
const restored = textpress.decompress(compressed.data);

console.log(restored.data); // Uint8Array containing the original bytes
```

`compress` accepts `.huff`, `.lz78`, or `.hlz` and returns data plus compression statistics. `decompress` reads the algorithm from the ZPCM header and returns the restored data plus metadata. Advanced consumers can use the documented C ABI in [Bindings/textpress_wasm.h](Bindings/textpress_wasm.h) directly.

---

## 📖 Commands

| Command | Usage | Description |
|---|---|---|
| `compress` | `zipcompress compress <input> <output>` | Compress a file |
| `decompress` | `zipcompress decompress <input> <output>` | Decompress a file |
| `help` | `zipcompress help` | Show all available commands |

The **algorithm is auto-detected** from the output file extension:

| Extension | Algorithm |
|---|---|
| `.huff` | Huffman coding |
| `.lz78` | LZ78 dictionary compression |
| `.hlz` | Adaptive Huffman-LZ78 hybrid |

---

## 🔧 How It Works

### Huffman Coding
Assigns shorter binary codes to more frequent bytes. Best for files with uneven character distributions.

### LZ78
Builds a dictionary of repeated phrases on the fly. Best for files with lots of repeated text patterns.

### Huffman-LZ78 Hybrid
Tries raw, Huffman, byte-level LZ78, LZ78-then-Huffman, and Huffman-then-LZ78 payloads, then stores the smallest lossless result.

### Compressed File Format
All compressed files use a binary format with:
- `ZPCM` magic header (4 bytes)
- Algorithm identifier (1 byte)
- Original file size (4 bytes)
- Algorithm-specific payload

The C++ implementation preserves this format and can decompress files created by the prior Python version.

---

## 👥 Contributors

[Jinisha Tejura ](https://github.com/jinishatejura)
[Vedant Tapkir]((https://github.com/OctaFlick))
