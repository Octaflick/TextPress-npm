#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace textpress {

constexpr std::uint8_t ALGO_HUFFMAN = 0x01;
constexpr std::uint8_t ALGO_LZ78 = 0x02;
constexpr std::uint8_t ALGO_HUFFMAN_LZ78 = 0x03;

struct CompressionStats {
    std::string algorithm;
    std::uint64_t original_size;
    std::uint64_t compressed_size;
    double ratio;
};

struct DecompressionStats {
    std::string algorithm;
    std::uint64_t original_size;
    std::uint64_t restored_size;
};

extern const std::unordered_map<std::string, std::uint8_t> EXTENSION_TO_ALGO;
extern const std::unordered_map<std::uint8_t, std::string> ALGO_NAMES;

std::uint8_t detect_algorithm(const std::string& filepath);
std::vector<std::uint8_t> compress_data(const std::vector<std::uint8_t>& data,
                                        const std::string& output_path);
std::vector<std::uint8_t> decompress_data(const std::vector<std::uint8_t>& compressed);
CompressionStats compress_file(const std::string& input_path, const std::string& output_path);
DecompressionStats decompress_file(const std::string& input_path, const std::string& output_path);

}  // namespace textpress
