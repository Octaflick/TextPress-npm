#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace textpress {

struct LZ78Token {
    std::uint64_t index;
    std::string ch;
};

std::vector<LZ78Token> lz78_compress(const std::string& text);
std::string lz78_decompress(const std::vector<LZ78Token>& tokens);
std::vector<std::pair<std::uint64_t, std::string>> tokens_to_pairs(
    const std::vector<LZ78Token>& tokens);

}  // namespace textpress
