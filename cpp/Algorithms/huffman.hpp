#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace textpress {

struct HuffmanNode {
    int ch;
    std::uint64_t freq;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    HuffmanNode(int character, std::uint64_t frequency,
                std::shared_ptr<HuffmanNode> left_node = nullptr,
                std::shared_ptr<HuffmanNode> right_node = nullptr);

    bool is_leaf() const;
};

class HuffmanTreeBuilder {
public:
    std::shared_ptr<HuffmanNode> build(
        const std::vector<std::pair<unsigned char, std::uint64_t>>& frequencies) const;
};

class HuffmanDecoder {
public:
    explicit HuffmanDecoder(std::shared_ptr<HuffmanNode> root = nullptr);

    void set_tree(std::shared_ptr<HuffmanNode> root);
    std::string decode(const std::string& encoded_bits) const;

private:
    std::shared_ptr<HuffmanNode> root_;
};

class HuffmanEncoder {
public:
    std::unordered_map<unsigned char, std::string> code_table;
    std::shared_ptr<HuffmanNode> root;

    void build_from_data(const std::string& data);
    void build_from_bytes(const std::vector<std::uint8_t>& data);
    std::string encode(const std::string& data) const;
    std::string encode_bytes(const std::vector<std::uint8_t>& data) const;
    std::unordered_map<unsigned char, std::uint64_t> get_frequency_table() const;

private:
    void build_codes(const std::shared_ptr<HuffmanNode>& node, const std::string& prefix);
    void collect_frequencies(const std::shared_ptr<HuffmanNode>& node,
                             std::unordered_map<unsigned char, std::uint64_t>& frequencies) const;
};

}  // namespace textpress
