#include "huffman_lz78.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace textpress {
namespace {

struct HuffmanNode {
    int symbol;
    std::uint64_t freq;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    bool is_leaf() const { return left == nullptr && right == nullptr; }
};

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::invalid_argument("Invalid Huffman payload: truncated header");
    }
    return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::invalid_argument("Invalid Huffman payload: truncated frequency table");
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

void write_varint(std::vector<std::uint8_t>& output, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte = static_cast<std::uint8_t>(byte | 0x80);
        }
        output.push_back(byte);
    } while (value != 0);
}

std::uint64_t read_varint(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    std::uint64_t result = 0;
    unsigned int shift = 0;
    while (true) {
        if (offset >= data.size()) {
            throw std::invalid_argument("Invalid varint: reached end of payload");
        }
        const std::uint8_t byte = data[offset++];
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return result;
        }
        shift += 7;
        if (shift > 63) {
            throw std::invalid_argument("Invalid varint: value is too large");
        }
    }
}

std::shared_ptr<HuffmanNode> build_huffman_tree(const std::array<std::uint64_t, 256>& frequencies) {
    struct QueueEntry {
        std::uint64_t frequency;
        std::uint64_t sequence;
        std::shared_ptr<HuffmanNode> node;
    };
    struct QueueEntryGreater {
        bool operator()(const QueueEntry& left, const QueueEntry& right) const {
            if (left.frequency != right.frequency) {
                return left.frequency > right.frequency;
            }
            return left.sequence > right.sequence;
        }
    };

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueEntryGreater> heap;
    std::uint64_t sequence = 0;
    for (int symbol = 0; symbol < 256; ++symbol) {
        if (frequencies[static_cast<std::size_t>(symbol)] != 0) {
            heap.push({frequencies[static_cast<std::size_t>(symbol)], sequence++,
                       std::make_shared<HuffmanNode>(HuffmanNode{symbol, frequencies[static_cast<std::size_t>(symbol)], nullptr, nullptr})});
        }
    }
    if (heap.empty()) {
        return nullptr;
    }
    while (heap.size() > 1) {
        const QueueEntry left = heap.top();
        heap.pop();
        const QueueEntry right = heap.top();
        heap.pop();
        const auto merged = std::make_shared<HuffmanNode>(
            HuffmanNode{-1, left.frequency + right.frequency, left.node, right.node});
        heap.push({merged->freq, sequence++, merged});
    }
    return heap.top().node;
}

void build_huffman_codes(const std::shared_ptr<HuffmanNode>& node,
                         std::array<std::string, 256>& codes,
                         const std::string& prefix) {
    if (node == nullptr) {
        return;
    }
    if (node->is_leaf()) {
        if (node->symbol < 0) {
            throw std::invalid_argument("Invalid Huffman tree: leaf without a symbol");
        }
        codes[static_cast<std::size_t>(node->symbol)] = prefix.empty() ? "0" : prefix;
        return;
    }
    build_huffman_codes(node->left, codes, prefix + "0");
    build_huffman_codes(node->right, codes, prefix + "1");
}

std::vector<std::uint8_t> huffman_encode(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return {0, 0, 0};
    }

    std::array<std::uint64_t, 256> frequencies{};
    for (std::uint8_t symbol : data) {
        ++frequencies[symbol];
    }
    const std::shared_ptr<HuffmanNode> root = build_huffman_tree(frequencies);
    std::array<std::string, 256> codes;
    build_huffman_codes(root, codes, "");

    std::vector<std::uint8_t> encoded_bits;
    std::uint8_t current_byte = 0;
    unsigned int bit_count = 0;
    for (std::uint8_t symbol : data) {
        for (char bit : codes[symbol]) {
            current_byte = static_cast<std::uint8_t>((current_byte << 1) | (bit == '1' ? 1 : 0));
            ++bit_count;
            if (bit_count == 8) {
                encoded_bits.push_back(current_byte);
                current_byte = 0;
                bit_count = 0;
            }
        }
    }

    std::uint8_t padding = 0;
    if (bit_count != 0) {
        padding = static_cast<std::uint8_t>(8 - bit_count);
        encoded_bits.push_back(static_cast<std::uint8_t>(current_byte << padding));
    }

    std::vector<std::uint8_t> result;
    result.reserve(3 + 5 * 256 + encoded_bits.size());
    result.push_back(padding);
    append_u16(result, 0);
    std::uint16_t symbol_count = 0;
    for (std::uint64_t frequency : frequencies) {
        if (frequency != 0) {
            ++symbol_count;
        }
    }
    result[1] = static_cast<std::uint8_t>(symbol_count >> 8);
    result[2] = static_cast<std::uint8_t>(symbol_count);
    for (int symbol = 0; symbol < 256; ++symbol) {
        const std::uint64_t frequency = frequencies[static_cast<std::size_t>(symbol)];
        if (frequency == 0) {
            continue;
        }
        if (frequency > UINT32_MAX) {
            throw std::invalid_argument("Huffman payload cannot represent frequencies above 4 GiB");
        }
        result.push_back(static_cast<std::uint8_t>(symbol));
        append_u32(result, static_cast<std::uint32_t>(frequency));
    }
    result.insert(result.end(), encoded_bits.begin(), encoded_bits.end());
    return result;
}

std::vector<std::uint8_t> huffman_decode(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 3) {
        throw std::invalid_argument("Invalid Huffman payload: missing header");
    }
    const std::uint8_t padding = payload[0];
    const std::uint16_t symbol_count = read_u16(payload, 1);
    if (padding > 7) {
        throw std::invalid_argument("Invalid Huffman payload: padding must be 0-7 bits");
    }

    std::size_t offset = 3;
    std::array<std::uint64_t, 256> frequencies{};
    for (std::uint16_t count = 0; count < symbol_count; ++count) {
        if (offset + 5 > payload.size()) {
            throw std::invalid_argument("Invalid Huffman payload: truncated frequency table");
        }
        const std::uint8_t symbol = payload[offset++];
        const std::uint32_t frequency = read_u32(payload, offset);
        offset += 4;
        if (frequency == 0) {
            throw std::invalid_argument("Invalid Huffman payload: symbol frequency must be positive");
        }
        frequencies[symbol] = frequency;
    }

    std::uint64_t expected_size = 0;
    for (std::uint64_t frequency : frequencies) {
        expected_size += frequency;
    }
    if (expected_size == 0) {
        return {};
    }
    if (symbol_count == 1) {
        for (int symbol = 0; symbol < 256; ++symbol) {
            if (frequencies[static_cast<std::size_t>(symbol)] != 0) {
                return std::vector<std::uint8_t>(static_cast<std::size_t>(expected_size),
                                                 static_cast<std::uint8_t>(symbol));
            }
        }
    }

    const std::shared_ptr<HuffmanNode> root = build_huffman_tree(frequencies);
    std::shared_ptr<HuffmanNode> current = root;
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(expected_size));
    for (; offset < payload.size(); ++offset) {
        const std::uint8_t packed_byte = payload[offset];
        for (int bit_index = 7; bit_index >= 0; --bit_index) {
            const bool bit = ((packed_byte >> bit_index) & 1) != 0;
            current = bit ? current->right : current->left;
            if (current == nullptr) {
                throw std::invalid_argument("Invalid Huffman payload: walked into a missing branch");
            }
            if (current->is_leaf()) {
                if (current->symbol < 0) {
                    throw std::invalid_argument("Invalid Huffman tree: leaf without a symbol");
                }
                output.push_back(static_cast<std::uint8_t>(current->symbol));
                if (output.size() == expected_size) {
                    return output;
                }
                current = root;
            }
        }
    }
    throw std::invalid_argument("Invalid Huffman payload: encoded data ended early");
}

std::vector<std::uint8_t> lz78_encode(const std::vector<std::uint8_t>& data) {
    std::unordered_map<std::string, std::uint64_t> dictionary;
    std::vector<std::uint8_t> token_stream;
    std::string current;
    std::uint64_t next_index = 1;

    for (std::uint8_t byte : data) {
        const std::string candidate = current + static_cast<char>(byte);
        const auto found = dictionary.find(candidate);
        if (found != dictionary.end()) {
            current = candidate;
            continue;
        }
        const auto previous = dictionary.find(current);
        write_varint(token_stream, previous == dictionary.end() ? 0 : previous->second);
        write_varint(token_stream, static_cast<std::uint64_t>(byte) + 1);
        dictionary[candidate] = next_index++;
        current.clear();
    }
    if (!current.empty()) {
        write_varint(token_stream, dictionary.at(current));
        write_varint(token_stream, 0);
    }
    return token_stream;
}

std::vector<std::uint8_t> lz78_decode(const std::vector<std::uint8_t>& payload) {
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> dictionary;
    dictionary.emplace(0, std::vector<std::uint8_t>{});
    std::vector<std::uint8_t> output;
    std::size_t offset = 0;
    std::uint64_t next_index = 1;

    while (offset < payload.size()) {
        const std::uint64_t index = read_varint(payload, offset);
        const std::uint64_t marker = read_varint(payload, offset);
        const auto found = dictionary.find(index);
        if (found == dictionary.end()) {
            throw std::invalid_argument("Invalid LZ78 payload: unknown dictionary index " +
                                        std::to_string(index));
        }
        if (marker > 256) {
            throw std::invalid_argument("Invalid LZ78 payload: byte marker is out of range");
        }

        std::vector<std::uint8_t> phrase = found->second;
        if (marker != 0) {
            phrase.push_back(static_cast<std::uint8_t>(marker - 1));
        }
        output.insert(output.end(), phrase.begin(), phrase.end());
        if (marker != 0) {
            dictionary[next_index++] = std::move(phrase);
        }
    }
    return output;
}

}  // namespace

std::vector<std::uint8_t> hybrid_compress(const std::vector<std::uint8_t>& data) {
    const std::vector<std::uint8_t> huffman_payload = huffman_encode(data);
    const std::vector<std::uint8_t> lz78_payload = lz78_encode(data);
    const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> candidates = {
        {MODE_RAW, data},
        {MODE_HUFFMAN, huffman_payload},
        {MODE_LZ78, lz78_payload},
        {MODE_LZ78_THEN_HUFFMAN, huffman_encode(lz78_payload)},
        {MODE_HUFFMAN_THEN_LZ78, lz78_encode(huffman_payload)},
    };

    const auto best = std::min_element(candidates.begin(), candidates.end(),
                                       [](const auto& left, const auto& right) {
                                           return left.second.size() + 1 < right.second.size() + 1;
                                       });
    std::vector<std::uint8_t> result;
    result.reserve(best->second.size() + 1);
    result.push_back(best->first);
    result.insert(result.end(), best->second.begin(), best->second.end());
    return result;
}

std::vector<std::uint8_t> hybrid_decompress(const std::vector<std::uint8_t>& payload) {
    if (payload.empty()) {
        throw std::invalid_argument("Invalid hybrid payload: missing compression mode");
    }
    const std::uint8_t mode = payload[0];
    const std::vector<std::uint8_t> data(payload.begin() + 1, payload.end());
    switch (mode) {
        case MODE_RAW:
            return data;
        case MODE_HUFFMAN:
            return huffman_decode(data);
        case MODE_LZ78:
            return lz78_decode(data);
        case MODE_LZ78_THEN_HUFFMAN:
            return lz78_decode(huffman_decode(data));
        case MODE_HUFFMAN_THEN_LZ78:
            return huffman_decode(lz78_decode(data));
        default:
            throw std::invalid_argument("Invalid hybrid payload: unknown compression mode " +
                                        std::to_string(mode));
    }
}

}  // namespace textpress
