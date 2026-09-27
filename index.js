"use strict";

const createTextPressModule = require("./textpress.js");

const textDecoder = new TextDecoder();
const ALGORITHMS = {
  1: "Huffman",
  2: "LZ78",
  3: "Huffman-LZ78 Hybrid"
};
const FORMATS = new Set([".huff", ".lz78", ".hlz"]);

/**
 * Instantiate the TextPress WASM module.
 *
 * @param {Record<string, unknown> & { locateFile?: (file: string, prefix: string) => string }} [options]
 */
async function createTextPress(options = {}) {
  const { locateFile, ...moduleOptions } = options;
  const factoryOptions = { ...moduleOptions };
  if (locateFile !== undefined) factoryOptions.locateFile = locateFile;
  const module = await createTextPressModule(factoryOptions);

  return Object.freeze({
    version: readCString(module, module._textpress_version()),
    compress(input, format) {
      const extension = normaliseFormat(format);
      const data = runBinaryOperation(
        module,
        "_textpress_compress",
        toUint8Array(input),
        encodeCString(`output${extension}`)
      );
      return compressionResult(data, input.byteLength);
    },
    decompress(input) {
      const compressed = toUint8Array(input);
      const header = parseHeader(compressed);
      const data = runBinaryOperation(module, "_textpress_decompress", compressed);
      return Object.freeze({
        data,
        algorithm: header.algorithm,
        originalSize: header.originalSize,
        restoredSize: data.byteLength
      });
    }
  });
}

module.exports = { createTextPress };

function runBinaryOperation(module, functionName, input, argument = undefined) {
  const inputPointer = input.byteLength === 0 ? 0 : module._malloc(input.byteLength);
  const argumentPointer = argument === undefined ? 0 : module._malloc(argument.byteLength);
  const outputPointerSlot = module._malloc(4);
  const outputSizeSlot = module._malloc(4);

  try {
    if (inputPointer !== 0) {
      module.HEAPU8.set(input, inputPointer);
    }
    if (argumentPointer !== 0) {
      module.HEAPU8.set(argument, argumentPointer);
    }
    const status = argument === undefined
      ? module[functionName](inputPointer, input.byteLength, outputPointerSlot, outputSizeSlot)
      : module[functionName](inputPointer, input.byteLength, argumentPointer, outputPointerSlot, outputSizeSlot);
    if (status !== 0) {
      throw new Error(readCString(module, module._textpress_last_error()) || "TextPress operation failed");
    }

    const outputPointer = module.HEAPU32[outputPointerSlot >>> 2];
    const outputSize = module.HEAPU32[outputSizeSlot >>> 2];
    const result = outputSize === 0
      ? new Uint8Array()
      : module.HEAPU8.slice(outputPointer, outputPointer + outputSize);
    if (outputPointer !== 0) {
      module._textpress_free_buffer(outputPointer);
    }
    return result;
  } finally {
    if (inputPointer !== 0) module._free(inputPointer);
    if (argumentPointer !== 0) module._free(argumentPointer);
    module._free(outputPointerSlot);
    module._free(outputSizeSlot);
  }
}

function compressionResult(data, originalSize) {
  const header = parseHeader(data);
  return Object.freeze({
    data,
    algorithm: header.algorithm,
    originalSize,
    compressedSize: data.byteLength,
    ratio: originalSize === 0 ? 0 : (1 - data.byteLength / originalSize) * 100
  });
}

function normaliseFormat(format) {
  const extension = String(format).toLowerCase();
  if (!FORMATS.has(extension)) {
    throw new TypeError("format must be one of: .huff, .lz78, .hlz");
  }
  return extension;
}

function toUint8Array(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  if (ArrayBuffer.isView(input)) {
    return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  }
  throw new TypeError("input must be an ArrayBuffer or typed-array view");
}

function encodeCString(value) {
  const encoded = new TextEncoder().encode(value);
  const cString = new Uint8Array(encoded.byteLength + 1);
  cString.set(encoded);
  return cString;
}

function readCString(module, pointer) {
  if (pointer === 0) return "";
  let end = pointer;
  while (module.HEAPU8[end] !== 0) ++end;
  return textDecoder.decode(module.HEAPU8.subarray(pointer, end));
}

function parseHeader(data) {
  if (data.byteLength < 9 || data[0] !== 0x5a || data[1] !== 0x50 ||
      data[2] !== 0x43 || data[3] !== 0x4d || ALGORITHMS[data[4]] === undefined) {
    throw new Error("Invalid TextPress ZPCM data");
  }
  const originalSize = new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(5, false);
  return { algorithm: ALGORITHMS[data[4]], originalSize };
}
