const test = require("node:test");
const assert = require("node:assert/strict");

const { createTextPress } = require("../dist");

test("compresses and decompresses all formats", async () => {
  const textpress = await createTextPress();
  const input = Buffer.from("Hello from TextPress! ".repeat(100));

  for (const format of [".huff", ".lz78", ".hlz"]) {
    const compressed = textpress.compress(input, format);
    const restored = textpress.decompress(compressed.data);

    assert.deepEqual(Buffer.from(restored.data), input);
    assert.equal(restored.originalSize, input.length);
  }
});
