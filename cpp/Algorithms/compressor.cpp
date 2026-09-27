#include "compressor.hpp"

#include "LZ78.hpp"
#include "huffman.hpp"
#include "huffman_lz78.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace textpress {

const std::unordered_map<std::string, std::uint8_t> EXTENSION_TO_ALGO = {
    {".huff", ALGO_HUFFMAN},
    {".lz78", ALGO_LZ78},
    {".hlz", ALGO_HUFFMAN_LZ78},
};

const std::unordered_map<std::uint8_t, std::string> ALGO_NAMES = {
    {ALGO_HUFFMAN, "Huffman"},
    {ALGO_LZ78, "LZ78"},
    {ALGO_HUFFMAN_LZ78, "Huffman-LZ78 Hybrid"},
};

namespace {

constexpr char MAGIC[] = "ZPCM";

std::vector<std::uint8_t> read_binary_file(const std::string& path, const std::string& missing_message) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(missing_message + path);
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Unable to determine file size: " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file && !data.empty()) {
        throw std::runtime_error("Unable to read file: " + path);
    }
    return data;
}

void write_binary_file(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Unable to write file: " + path);
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        throw std::runtime_error("Unable to write file: " + path);
    }
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset,
                       const std::string& error_message) {
    if (offset + 4 > data.size()) {
        throw std::invalid_argument(error_message);
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

std::string lower_copy(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string extension_of(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) {
        return "";
    }
    return lower_copy(path.substr(dot));
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        throw std::invalid_argument("Invalid Unicode code point");
    }
}

bool is_continuation(std::uint8_t byte) {
    return (byte & 0xC0) == 0x80;
}

std::string decode_utf8_with_replacement(const std::vector<std::uint8_t>& data) {
    std::string output;
    for (std::size_t offset = 0; offset < data.size();) {
        const std::uint8_t first = data[offset];
        if (first <= 0x7F) {
            output.push_back(static_cast<char>(first));
            ++offset;
            continue;
        }

        std::size_t length = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
        }
        if (length == 0) {
            append_utf8(output, 0xFFFD);
            ++offset;
            continue;
        }

        std::size_t valid_prefix = 1;
        while (valid_prefix < length && offset + valid_prefix < data.size()) {
            const std::uint8_t next = data[offset + valid_prefix];
            bool valid = is_continuation(next);
            if (valid && valid_prefix == 1) {
                if (first == 0xE0) {
                    valid = next >= 0xA0;
                } else if (first == 0xED) {
                    valid = next <= 0x9F;
                } else if (first == 0xF0) {
                    valid = next >= 0x90;
                } else if (first == 0xF4) {
                    valid = next <= 0x8F;
                }
            }
            if (!valid) {
                break;
            }
            ++valid_prefix;
        }
        if (valid_prefix != length) {
            append_utf8(output, 0xFFFD);
            offset += valid_prefix;
            continue;
        }

        std::uint32_t code_point = 0;
        if (length == 2) {
            code_point = ((first & 0x1F) << 6) | (data[offset + 1] & 0x3F);
        } else if (length == 3) {
            code_point = ((first & 0x0F) << 12) | ((data[offset + 1] & 0x3F) << 6) |
                         (data[offset + 2] & 0x3F);
        } else {
            code_point = ((first & 0x07) << 18) | ((data[offset + 1] & 0x3F) << 12) |
                         ((data[offset + 2] & 0x3F) << 6) | (data[offset + 3] & 0x3F);
        }
        append_utf8(output, code_point);
        offset += length;
    }
    return output;
}

std::uint32_t read_utf8_scalar(const std::string& text, std::size_t& offset) {
    if (offset >= text.size()) {
        throw std::invalid_argument("Invalid UTF-8 string");
    }
    const std::uint8_t first = static_cast<std::uint8_t>(text[offset++]);
    if (first <= 0x7F) {
        return first;
    }
    std::size_t length = 0;
    std::uint32_t code_point = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        code_point = first & 0x1F;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        code_point = first & 0x0F;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        code_point = first & 0x07;
    } else {
        throw std::invalid_argument("Invalid UTF-8 string");
    }
    if (offset + length - 1 > text.size()) {
        throw std::invalid_argument("Invalid UTF-8 string");
    }
    for (std::size_t count = 1; count < length; ++count) {
        const std::uint8_t next = static_cast<std::uint8_t>(text[offset++]);
        if (!is_continuation(next)) {
            throw std::invalid_argument("Invalid UTF-8 string");
        }
        code_point = (code_point << 6) | (next & 0x3F);
    }
    return code_point;
}

std::string hex4(std::uint16_t value) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string result(4, '0');
    for (int position = 3; position >= 0; --position) {
        result[static_cast<std::size_t>(position)] = HEX[value & 0x0F];
        value = static_cast<std::uint16_t>(value >> 4);
    }
    return result;
}

std::string json_escape(const std::string& text) {
    std::string output;
    output.push_back('"');
    for (std::size_t offset = 0; offset < text.size();) {
        const std::uint32_t code_point = read_utf8_scalar(text, offset);
        switch (code_point) {
            case '"': output += "\\\""; continue;
            case '\\': output += "\\\\"; continue;
            case '\b': output += "\\b"; continue;
            case '\f': output += "\\f"; continue;
            case '\n': output += "\\n"; continue;
            case '\r': output += "\\r"; continue;
            case '\t': output += "\\t"; continue;
            default: break;
        }
        if (code_point < 0x20) {
            output += "\\u" + hex4(static_cast<std::uint16_t>(code_point));
        } else if (code_point <= 0x7F) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0xFFFF) {
            output += "\\u" + hex4(static_cast<std::uint16_t>(code_point));
        } else {
            const std::uint32_t adjusted = code_point - 0x10000;
            output += "\\u" + hex4(static_cast<std::uint16_t>(0xD800 + (adjusted >> 10)));
            output += "\\u" + hex4(static_cast<std::uint16_t>(0xDC00 + (adjusted & 0x3FF)));
        }
    }
    output.push_back('"');
    return output;
}

class JsonReader {
public:
    explicit JsonReader(const std::string& input) : input_(input) {}

    void skip_whitespace() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    void expect(char expected) {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            throw std::invalid_argument("Invalid JSON payload");
        }
        ++position_;
    }

    bool consume(char expected) {
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    std::uint64_t parse_uint() {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            throw std::invalid_argument("Invalid JSON number");
        }
        std::uint64_t value = 0;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            const std::uint8_t digit = static_cast<std::uint8_t>(input_[position_++] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                throw std::invalid_argument("JSON number is too large");
            }
            value = value * 10 + digit;
        }
        return value;
    }

    std::string parse_string() {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"') {
            throw std::invalid_argument("Invalid JSON string");
        }
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return output;
            }
            if (character < 0x20) {
                throw std::invalid_argument("Invalid JSON string control character");
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Invalid JSON escape");
            }
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code_point = parse_hex4();
                    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                        if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') {
                            throw std::invalid_argument("Invalid JSON surrogate pair");
                        }
                        position_ += 2;
                        const std::uint32_t low = parse_hex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            throw std::invalid_argument("Invalid JSON surrogate pair");
                        }
                        code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                        throw std::invalid_argument("Invalid JSON surrogate pair");
                    }
                    append_utf8(output, code_point);
                    break;
                }
                default:
                    throw std::invalid_argument("Invalid JSON escape");
            }
        }
        throw std::invalid_argument("Unterminated JSON string");
    }

    bool at_end() {
        skip_whitespace();
        return position_ == input_.size();
    }

private:
    std::uint32_t parse_hex4() {
        if (position_ + 4 > input_.size()) {
            throw std::invalid_argument("Invalid JSON Unicode escape");
        }
        std::uint32_t value = 0;
        for (int count = 0; count < 4; ++count) {
            const char character = input_[position_++];
            value <<= 4;
            if (character >= '0' && character <= '9') {
                value |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                throw std::invalid_argument("Invalid JSON Unicode escape");
            }
        }
        return value;
    }

    const std::string& input_;
    std::size_t position_ = 0;
};

std::string serialize_huffman_table(const std::unordered_map<unsigned char, std::string>& code_table) {
    std::string json = "{";
    bool first = true;
    for (int character = 0; character < 256; ++character) {
        const auto found = code_table.find(static_cast<unsigned char>(character));
        if (found == code_table.end()) {
            continue;
        }
        if (!first) {
            json.push_back(',');
        }
        first = false;
        json += "\"" + std::to_string(character) + "\":\"" + found->second + "\"";
    }
    json.push_back('}');
    return json;
}

std::unordered_map<std::string, std::uint8_t> parse_huffman_table(const std::string& json) {
    JsonReader reader(json);
    reader.expect('{');
    std::unordered_map<std::string, std::uint8_t> reverse_table;
    if (reader.consume('}')) {
        if (!reader.at_end()) {
            throw std::invalid_argument("Invalid Huffman code table JSON");
        }
        return reverse_table;
    }
    while (true) {
        const std::string ordinal_text = reader.parse_string();
        reader.expect(':');
        const std::string code = reader.parse_string();
        std::size_t consumed = 0;
        unsigned long ordinal = 0;
        try {
            ordinal = std::stoul(ordinal_text, &consumed);
        } catch (const std::exception&) {
            throw std::invalid_argument("Invalid Huffman code table character ordinal");
        }
        if (consumed != ordinal_text.size() || ordinal > 255 || code.empty()) {
            throw std::invalid_argument("Invalid Huffman code table entry");
        }
        for (char bit : code) {
            if (bit != '0' && bit != '1') {
                throw std::invalid_argument("Invalid Huffman code table bit code");
            }
        }
        reverse_table[code] = static_cast<std::uint8_t>(ordinal);
        if (reader.consume('}')) {
            break;
        }
        reader.expect(',');
    }
    if (!reader.at_end()) {
        throw std::invalid_argument("Invalid Huffman code table JSON");
    }
    return reverse_table;
}

std::vector<std::uint8_t> huffman_compress_payload(const std::vector<std::uint8_t>& data) {
    HuffmanEncoder encoder;
    encoder.build_from_bytes(data);
    const std::string bit_string = encoder.encode_bytes(data);
    const std::string code_json = serialize_huffman_table(encoder.code_table);

    const std::uint8_t padding = static_cast<std::uint8_t>((8 - bit_string.size() % 8) % 8);
    std::vector<std::uint8_t> encoded_bytes;
    encoded_bytes.reserve((bit_string.size() + padding) / 8);
    std::uint8_t current_byte = 0;
    unsigned int bit_count = 0;
    for (char bit : bit_string) {
        current_byte = static_cast<std::uint8_t>((current_byte << 1) | (bit == '1' ? 1 : 0));
        if (++bit_count == 8) {
            encoded_bytes.push_back(current_byte);
            current_byte = 0;
            bit_count = 0;
        }
    }
    if (bit_count != 0) {
        encoded_bytes.push_back(static_cast<std::uint8_t>(current_byte << padding));
    }

    if (code_json.size() > UINT32_MAX) {
        throw std::invalid_argument("Huffman code table is too large");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(5 + code_json.size() + encoded_bytes.size());
    payload.push_back(padding);
    append_u32(payload, static_cast<std::uint32_t>(code_json.size()));
    payload.insert(payload.end(), code_json.begin(), code_json.end());
    payload.insert(payload.end(), encoded_bytes.begin(), encoded_bytes.end());
    return payload;
}

std::vector<std::uint8_t> huffman_decompress_payload(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 5) {
        throw std::invalid_argument("Invalid Huffman payload: missing header");
    }
    const std::uint8_t padding = payload[0];
    if (padding > 7) {
        throw std::invalid_argument("Invalid Huffman payload: padding must be 0-7 bits");
    }
    const std::uint32_t code_json_length = read_u32(payload, 1, "Invalid Huffman payload: missing code table length");
    const std::size_t offset = 5;
    if (code_json_length > payload.size() - offset) {
        throw std::invalid_argument("Invalid Huffman payload: truncated code table");
    }
    const std::string code_json(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                payload.begin() + static_cast<std::ptrdiff_t>(offset + code_json_length));
    const auto reverse_table = parse_huffman_table(code_json);
    const std::size_t encoded_offset = offset + code_json_length;
    if (encoded_offset == payload.size()) {
        return {};
    }

    const std::size_t bit_count = (payload.size() - encoded_offset) * 8 - padding;
    std::vector<std::uint8_t> decoded;
    std::string current_code;
    current_code.reserve(256);
    for (std::size_t bit_number = 0; bit_number < bit_count; ++bit_number) {
        const std::uint8_t packed = payload[encoded_offset + bit_number / 8];
        const char bit = ((packed >> (7 - (bit_number % 8))) & 1) == 0 ? '0' : '1';
        current_code.push_back(bit);
        const auto found = reverse_table.find(current_code);
        if (found != reverse_table.end()) {
            decoded.push_back(found->second);
            current_code.clear();
        }
    }
    if (!current_code.empty()) {
        throw std::invalid_argument("Invalid encoded data: trailing bits don't form a valid code");
    }
    return decoded;
}

std::vector<std::uint8_t> lz78_compress_payload(const std::vector<std::uint8_t>& data) {
    const std::vector<LZ78Token> tokens = lz78_compress(decode_utf8_with_replacement(data));
    std::string json = "[";
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0) {
            json.push_back(',');
        }
        json += "[" + std::to_string(tokens[index].index) + "," + json_escape(tokens[index].ch) + "]";
    }
    json.push_back(']');
    return std::vector<std::uint8_t>(json.begin(), json.end());
}

std::vector<std::uint8_t> lz78_decompress_payload(const std::vector<std::uint8_t>& payload) {
    const std::string json(payload.begin(), payload.end());
    JsonReader reader(json);
    reader.expect('[');
    std::vector<LZ78Token> tokens;
    if (!reader.consume(']')) {
        while (true) {
            reader.expect('[');
            const std::uint64_t index = reader.parse_uint();
            reader.expect(',');
            const std::string character = reader.parse_string();
            reader.expect(']');
            tokens.push_back({index, character});
            if (reader.consume(']')) {
                break;
            }
            reader.expect(',');
        }
    }
    if (!reader.at_end()) {
        throw std::invalid_argument("Invalid LZ78 token JSON");
    }
    const std::string text = lz78_decompress(tokens);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

std::uint8_t detect_algorithm(const std::string& filepath) {
    const std::string extension = extension_of(filepath);
    const auto found = EXTENSION_TO_ALGO.find(extension);
    if (found == EXTENSION_TO_ALGO.end()) {
        throw std::invalid_argument("Unknown extension '" + extension +
                                    "'. Supported: .huff, .lz78, .hlz");
    }
    return found->second;
}

std::vector<std::uint8_t> compress_data(const std::vector<std::uint8_t>& data,
                                        const std::string& output_path) {
    if (data.size() > UINT32_MAX) {
        throw std::invalid_argument("Input file is too large for the ZPCM format (maximum 4 GiB)");
    }
    const std::uint8_t algorithm = detect_algorithm(output_path);
    std::vector<std::uint8_t> payload;
    switch (algorithm) {
        case ALGO_HUFFMAN:
            payload = huffman_compress_payload(data);
            break;
        case ALGO_LZ78:
            payload = lz78_compress_payload(data);
            break;
        case ALGO_HUFFMAN_LZ78:
            payload = hybrid_compress(data);
            break;
        default:
            throw std::invalid_argument("Unsupported algorithm id: " + std::to_string(algorithm));
    }

    std::vector<std::uint8_t> output;
    output.reserve(9 + payload.size());
    output.insert(output.end(), MAGIC, MAGIC + 4);
    output.push_back(algorithm);
    append_u32(output, static_cast<std::uint32_t>(data.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::uint8_t> decompress_data(const std::vector<std::uint8_t>& compressed) {
    if (compressed.size() < 9 || compressed[0] != 'Z' || compressed[1] != 'P' ||
        compressed[2] != 'C' || compressed[3] != 'M') {
        throw std::invalid_argument("Invalid file format: missing ZPCM magic header. "
                                    "Is this a file compressed by zipcompress?");
    }
    const std::uint8_t algorithm = compressed[4];
    if (ALGO_NAMES.find(algorithm) == ALGO_NAMES.end()) {
        throw std::invalid_argument("Unknown algorithm id in file header: " + std::to_string(algorithm));
    }
    const std::vector<std::uint8_t> payload(compressed.begin() + 9, compressed.end());
    switch (algorithm) {
        case ALGO_HUFFMAN:
            return huffman_decompress_payload(payload);
        case ALGO_LZ78:
            return lz78_decompress_payload(payload);
        case ALGO_HUFFMAN_LZ78:
            return hybrid_decompress(payload);
        default:
            throw std::invalid_argument("Unsupported algorithm id: " + std::to_string(algorithm));
    }
}

CompressionStats compress_file(const std::string& input_path, const std::string& output_path) {
    const std::vector<std::uint8_t> data = read_binary_file(input_path, "Input file not found: ");
    if (data.size() > UINT32_MAX) {
        throw std::invalid_argument("Input file is too large for the ZPCM format (maximum 4 GiB)");
    }
    const std::uint8_t algorithm = detect_algorithm(output_path);
    std::vector<std::uint8_t> payload;
    switch (algorithm) {
        case ALGO_HUFFMAN:
            payload = huffman_compress_payload(data);
            break;
        case ALGO_LZ78:
            payload = lz78_compress_payload(data);
            break;
        case ALGO_HUFFMAN_LZ78:
            payload = hybrid_compress(data);
            break;
        default:
            throw std::invalid_argument("Unsupported algorithm id: " + std::to_string(algorithm));
    }

    std::vector<std::uint8_t> output;
    output.reserve(9 + payload.size());
    output.insert(output.end(), MAGIC, MAGIC + 4);
    output.push_back(algorithm);
    append_u32(output, static_cast<std::uint32_t>(data.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    write_binary_file(output_path, output);

    const double ratio = data.empty() ? 0.0 :
        (1.0 - static_cast<double>(output.size()) / static_cast<double>(data.size())) * 100.0;
    return {ALGO_NAMES.at(algorithm), data.size(), output.size(), ratio};
}

DecompressionStats decompress_file(const std::string& input_path, const std::string& output_path) {
    const std::vector<std::uint8_t> compressed =
        read_binary_file(input_path, "Compressed file not found: ");
    if (compressed.size() < 9 || compressed[0] != 'Z' || compressed[1] != 'P' ||
        compressed[2] != 'C' || compressed[3] != 'M') {
        throw std::invalid_argument("Invalid file format: missing ZPCM magic header. "
                                    "Is this a file compressed by zipcompress?");
    }
    const std::uint8_t algorithm = compressed[4];
    const auto algorithm_name = ALGO_NAMES.find(algorithm);
    if (algorithm_name == ALGO_NAMES.end()) {
        throw std::invalid_argument("Unknown algorithm id in file header: " + std::to_string(algorithm));
    }
    const std::uint32_t original_size = read_u32(compressed, 5, "Invalid compressed file header");
    const std::vector<std::uint8_t> payload(compressed.begin() + 9, compressed.end());
    std::vector<std::uint8_t> data;
    switch (algorithm) {
        case ALGO_HUFFMAN:
            data = huffman_decompress_payload(payload);
            break;
        case ALGO_LZ78:
            data = lz78_decompress_payload(payload);
            break;
        case ALGO_HUFFMAN_LZ78:
            data = hybrid_decompress(payload);
            break;
        default:
            throw std::invalid_argument("Unsupported algorithm id: " + std::to_string(algorithm));
    }
    write_binary_file(output_path, data);
    return {algorithm_name->second, original_size, data.size()};
}

}  // namespace textpress
