#include "textpress_wasm.h"

#include "../Algorithms/compressor.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string last_error;

void validate_output_arguments(std::uint8_t** output, std::size_t* output_size) {
    if (output == nullptr || output_size == nullptr) {
        throw std::invalid_argument("Output buffer and output size pointers are required");
    }
}

std::vector<std::uint8_t> copy_input(const std::uint8_t* input, std::size_t input_size) {
    if (input == nullptr && input_size != 0) {
        throw std::invalid_argument("Input pointer is null but input size is non-zero");
    }
    if (input_size == 0) {
        return {};
    }
    return {input, input + input_size};
}

void copy_output(const std::vector<std::uint8_t>& data, std::uint8_t** output,
                 std::size_t* output_size) {
    *output = nullptr;
    *output_size = data.size();
    if (data.empty()) {
        return;
    }

    auto* allocated = static_cast<std::uint8_t*>(std::malloc(data.size()));
    if (allocated == nullptr) {
        throw std::bad_alloc();
    }
    std::memcpy(allocated, data.data(), data.size());
    *output = allocated;
}

template <typename Operation>
int run_operation(Operation&& operation) {
    try {
        last_error.clear();
        operation();
        return 0;
    } catch (const std::exception& error) {
        last_error = error.what();
    } catch (...) {
        last_error = "Unknown TextPress error";
    }
    return 1;
}

}  // namespace

extern "C" {

const char* textpress_version(void) {
    return "1.0.0";
}

const char* textpress_last_error(void) {
    return last_error.c_str();
}

int textpress_compress(const std::uint8_t* input, std::size_t input_size, const char* output_path,
                       std::uint8_t** output, std::size_t* output_size) {
    return run_operation([&] {
        validate_output_arguments(output, output_size);
        if (output_path == nullptr) {
            throw std::invalid_argument("Output path is required to select a compression format");
        }
        const auto compressed = textpress::compress_data(copy_input(input, input_size), output_path);
        copy_output(compressed, output, output_size);
    });
}

int textpress_decompress(const std::uint8_t* input, std::size_t input_size,
                         std::uint8_t** output, std::size_t* output_size) {
    return run_operation([&] {
        validate_output_arguments(output, output_size);
        const auto decompressed = textpress::decompress_data(copy_input(input, input_size));
        copy_output(decompressed, output, output_size);
    });
}

void textpress_free_buffer(std::uint8_t* buffer) {
    std::free(buffer);
}

}  // extern "C"
