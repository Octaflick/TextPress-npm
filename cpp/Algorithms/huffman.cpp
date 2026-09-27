#include "huffman.hpp"

#include <queue>
#include <stdexcept>

namespace textpress {

HuffmanNode::HuffmanNode(int character, std::uint64_t frequency,
                         std::shared_ptr<HuffmanNode> left_node,
                         std::shared_ptr<HuffmanNode> right_node)
    : ch(character), freq(frequency), left(std::move(left_node)), right(std::move(right_node)) {}

bool HuffmanNode::is_leaf() const {
    return left == nullptr && right == nullptr;
}

std::shared_ptr<HuffmanNode> HuffmanTreeBuilder::build(
    const std::vector<std::pair<unsigned char, std::uint64_t>>& frequencies) const {
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
    for (const auto& [character, frequency] : frequencies) {
        heap.push({frequency, sequence++, std::make_shared<HuffmanNode>(character, frequency)});
    }

    if (heap.empty()) {
        return nullptr;
    }

    while (heap.size() > 1) {
        QueueEntry left = heap.top();
        heap.pop();
        QueueEntry right = heap.top();
        heap.pop();
        const auto merged = std::make_shared<HuffmanNode>(
            -1, left.frequency + right.frequency, left.node, right.node);
        heap.push({merged->freq, sequence++, merged});
    }

    return heap.top().node;
}

HuffmanDecoder::HuffmanDecoder(std::shared_ptr<HuffmanNode> root) : root_(std::move(root)) {}

void HuffmanDecoder::set_tree(std::shared_ptr<HuffmanNode> root) {
    root_ = std::move(root);
}

std::string HuffmanDecoder::decode(const std::string& encoded_bits) const {
    if (root_ == nullptr) {
        throw std::invalid_argument("Huffman tree is empty");
    }

    std::string decoded;
    std::shared_ptr<HuffmanNode> current = root_;
    for (char bit : encoded_bits) {
        if (bit != '0' && bit != '1') {
            continue;
        }

        current = bit == '0' ? current->left : current->right;
        if (current == nullptr) {
            throw std::invalid_argument("Invalid encoded string: walked into a missing branch");
        }

        if (current->is_leaf()) {
            if (current->ch < 0) {
                throw std::invalid_argument("Invalid Huffman tree: leaf without a character");
            }
            decoded.push_back(static_cast<char>(current->ch));
            current = root_;
        }
    }

    if (current != root_) {
        throw std::invalid_argument("Invalid encoded string: trailing incomplete Huffman code");
    }
    return decoded;
}

void HuffmanEncoder::build_codes(const std::shared_ptr<HuffmanNode>& node, const std::string& prefix) {
    if (node == nullptr) {
        return;
    }
    if (node->is_leaf()) {
        if (node->ch < 0) {
            throw std::invalid_argument("Invalid Huffman tree: leaf without a character");
        }
        code_table[static_cast<unsigned char>(node->ch)] = prefix.empty() ? "0" : prefix;
        return;
    }
    build_codes(node->left, prefix + "0");
    build_codes(node->right, prefix + "1");
}

void HuffmanEncoder::build_from_data(const std::string& data) {
    std::vector<std::uint8_t> bytes(data.begin(), data.end());
    build_from_bytes(bytes);
}

void HuffmanEncoder::build_from_bytes(const std::vector<std::uint8_t>& data) {
    std::uint64_t counts[256] = {};
    bool seen[256] = {};
    std::vector<std::pair<unsigned char, std::uint64_t>> frequencies;

    for (std::uint8_t byte : data) {
        if (!seen[byte]) {
            seen[byte] = true;
            frequencies.emplace_back(byte, 0);
        }
        ++counts[byte];
    }
    for (auto& [character, frequency] : frequencies) {
        frequency = counts[character];
    }

    HuffmanTreeBuilder builder;
    root = builder.build(frequencies);
    code_table.clear();
    build_codes(root, "");
}

std::string HuffmanEncoder::encode(const std::string& data) const {
    std::vector<std::uint8_t> bytes(data.begin(), data.end());
    return encode_bytes(bytes);
}

std::string HuffmanEncoder::encode_bytes(const std::vector<std::uint8_t>& data) const {
    if (code_table.empty() && !data.empty()) {
        throw std::invalid_argument("Encoder not initialized. Call build_from_bytes first.");
    }

    std::string encoded_bits;
    for (std::uint8_t byte : data) {
        const auto found = code_table.find(byte);
        if (found == code_table.end()) {
            throw std::invalid_argument("Input contains a byte missing from the Huffman code table");
        }
        encoded_bits += found->second;
    }
    return encoded_bits;
}

void HuffmanEncoder::collect_frequencies(
    const std::shared_ptr<HuffmanNode>& node,
    std::unordered_map<unsigned char, std::uint64_t>& frequencies) const {
    if (node == nullptr) {
        return;
    }
    if (node->is_leaf()) {
        if (node->ch >= 0) {
            frequencies[static_cast<unsigned char>(node->ch)] = node->freq;
        }
        return;
    }
    collect_frequencies(node->left, frequencies);
    collect_frequencies(node->right, frequencies);
}

std::unordered_map<unsigned char, std::uint64_t> HuffmanEncoder::get_frequency_table() const {
    std::unordered_map<unsigned char, std::uint64_t> frequencies;
    collect_frequencies(root, frequencies);
    return frequencies;
}

}  // namespace textpress
