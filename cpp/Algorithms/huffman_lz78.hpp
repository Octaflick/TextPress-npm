#pragma once

#include <cstdint>
#include <vector>

namespace textpress {

constexpr std::uint8_t MODE_RAW = 0;
constexpr std::uint8_t MODE_HUFFMAN = 1;
constexpr std::uint8_t MODE_LZ78 = 2;
constexpr std::uint8_t MODE_LZ78_THEN_HUFFMAN = 3;
constexpr std::uint8_t MODE_HUFFMAN_THEN_LZ78 = 4;

std::vector<std::uint8_t> hybrid_compress(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> hybrid_decompress(const std::vector<std::uint8_t>& payload);

}  // namespace textpress
