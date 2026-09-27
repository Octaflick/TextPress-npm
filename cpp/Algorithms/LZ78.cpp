#include "LZ78.hpp"

#include <stdexcept>
#include <unordered_map>

namespace textpress {

std::vector<LZ78Token> lz78_compress(const std::string& text) {
    std::unordered_map<std::string, std::uint64_t> dictionary;
    std::vector<LZ78Token> tokens;
    std::string current;
    std::uint64_t next_index = 1;

    for (std::size_t offset = 0; offset < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        std::size_t char_length = 1;
        if ((first & 0xE0) == 0xC0) {
            char_length = 2;
        } else if ((first & 0xF0) == 0xE0) {
            char_length = 3;
        } else if ((first & 0xF8) == 0xF0) {
            char_length = 4;
        }
        if (offset + char_length > text.size()) {
            throw std::invalid_argument("LZ78 input contains a truncated UTF-8 character");
        }

        const std::string character = text.substr(offset, char_length);
        offset += char_length;
        const std::string candidate = current + character;
        if (dictionary.find(candidate) != dictionary.end()) {
            current = candidate;
            continue;
        }

        const auto previous = dictionary.find(current);
        tokens.push_back({previous == dictionary.end() ? 0 : previous->second, character});
        dictionary[candidate] = next_index++;
        current.clear();
    }

    if (!current.empty()) {
        tokens.push_back({dictionary.at(current), ""});
    }
    return tokens;
}

std::string lz78_decompress(const std::vector<LZ78Token>& tokens) {
    std::unordered_map<std::uint64_t, std::string> dictionary;
    dictionary.emplace(0, "");
    std::uint64_t next_index = 1;
    std::string output;

    for (const LZ78Token& token : tokens) {
        const auto found = dictionary.find(token.index);
        if (found == dictionary.end()) {
            throw std::invalid_argument("Invalid LZ78 token index: " + std::to_string(token.index));
        }
        const std::string phrase = found->second + token.ch;
        output += phrase;
        dictionary[next_index++] = phrase;
    }
    return output;
}

std::vector<std::pair<std::uint64_t, std::string>> tokens_to_pairs(
    const std::vector<LZ78Token>& tokens) {
    std::vector<std::pair<std::uint64_t, std::string>> pairs;
    pairs.reserve(tokens.size());
    for (const LZ78Token& token : tokens) {
        pairs.emplace_back(token.index, token.ch);
    }
    return pairs;
}

}  // namespace textpress
