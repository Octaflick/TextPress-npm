declare namespace TextPress {
  type CompressionFormat = ".huff" | ".lz78" | ".hlz";
  type BinaryInput = ArrayBuffer | ArrayBufferView;

interface CompressionResult {
  readonly data: Uint8Array;
  readonly algorithm: "Huffman" | "LZ78" | "Huffman-LZ78 Hybrid";
  readonly originalSize: number;
  readonly compressedSize: number;
  readonly ratio: number;
}

interface DecompressionResult {
  readonly data: Uint8Array;
  readonly algorithm: "Huffman" | "LZ78" | "Huffman-LZ78 Hybrid";
  readonly originalSize: number;
  readonly restoredSize: number;
}

interface Instance {
  readonly version: string;
  compress(input: BinaryInput, format: CompressionFormat): CompressionResult;
  decompress(input: BinaryInput): DecompressionResult;
}

interface ModuleOptions {
  locateFile?: (file: string, prefix: string) => string;
  wasmBinary?: ArrayBuffer;
}

function createTextPress(options?: ModuleOptions): Promise<Instance>;
}

export = TextPress;
