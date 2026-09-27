#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A stable C ABI for JavaScript/WASM consumers.
 *
 * Every function returning a status uses 0 for success. On failure, retrieve a
 * human-readable message with textpress_last_error(). Buffers returned through
 * output must be released with textpress_free_buffer().
 */

const char* textpress_version(void);
const char* textpress_last_error(void);

int textpress_compress(const uint8_t* input, size_t input_size, const char* output_path,
                       uint8_t** output, size_t* output_size);
int textpress_decompress(const uint8_t* input, size_t input_size,
                         uint8_t** output, size_t* output_size);

void textpress_free_buffer(uint8_t* buffer);

#ifdef __cplusplus
}
#endif
