#ifndef HUFFMAN_HPP
#define HUFFMAN_HPP

#include <bitset>
#include <queue>
#include <unordered_map>
#include <vector>

struct HuffmanNode {
    uint8_t data;
    int frequency;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    HuffmanNode(uint8_t d, int freq) : data(d), frequency(freq), left(nullptr), right(nullptr) {}
    HuffmanNode(int freq) : data(0), frequency(freq), left(nullptr), right(nullptr) {}
};

struct Compare {
    bool operator()(std::shared_ptr<HuffmanNode> a, std::shared_ptr<HuffmanNode> b) {
        return a->frequency > b->frequency;
    }
};

class Huffman {
private:
    static void generateCodes(std::shared_ptr<HuffmanNode> root, std::string code,
                             std::unordered_map<uint8_t, std::string>& codes) {
        if (!root) {
            return;
        }

        if (!root->left && !root->right) {
            codes[root->data] = code.empty() ? "0" : code;
            return;
        }

        generateCodes(root->left, code + "0", codes);
        generateCodes(root->right, code + "1", codes);
    }

public:
    static std::pair<std::vector<uint8_t>, std::shared_ptr<HuffmanNode>> compress(const std::vector<uint8_t>& data) {
        if (data.empty()) return {std::vector<uint8_t>(), nullptr};

        std::unordered_map<uint8_t, int> frequency;
        for (uint8_t byte : data) {
            frequency[byte]++;
        }

        std::priority_queue<std::shared_ptr<HuffmanNode>, std::vector<std::shared_ptr<HuffmanNode>>, Compare> pq;

        for (auto& pair : frequency) {
            pq.push(std::make_shared<HuffmanNode>(pair.first, pair.second));
        }

        while (pq.size() > 1) {
            auto left = pq.top(); pq.pop();
            auto right = pq.top(); pq.pop();

            auto merged = std::make_shared<HuffmanNode>(left->frequency + right->frequency);
            merged->left = left;
            merged->right = right;

            pq.push(merged);
        }

        auto root = pq.top();

        std::unordered_map<uint8_t, std::string> codes;
        generateCodes(root, "", codes);

        std::string encoded = "";
        for (uint8_t byte : data) {
            encoded += codes[byte];
        }

        std::vector<uint8_t> compressed;
        for (size_t i = 0; i < encoded.length(); i += 8) {
            std::string byte_str = encoded.substr(i, 8);
            while (byte_str.length() < 8) byte_str += "0";
            compressed.push_back(static_cast<uint8_t>(std::stoi(byte_str, 0, 2)));
        }

        return {compressed, root};
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed,
                                         std::shared_ptr<HuffmanNode> root, size_t original_bits) {
        if (!root || compressed.empty()) return std::vector<uint8_t>();

        std::vector<uint8_t> decompressed;
        std::string bit_string = "";

        for (uint8_t byte : compressed) {
            std::bitset<8> bits(byte);
            bit_string += bits.to_string();
        }

        if (original_bits > 0 && original_bits < bit_string.length()) {
            bit_string = bit_string.substr(0, original_bits);
        }

        auto current = root;
        for (char bit : bit_string) {
            if (bit == '0') {
                current = current->left;
            } else {
                current = current->right;
            }

            if (!current->left && !current->right) {
                decompressed.push_back(current->data);
                current = root;
            }
        }

        return decompressed;
    }
};

std::vector<uint8_t> serializeTree(std::shared_ptr<HuffmanNode> node) {
    std::vector<uint8_t> result;
    if (!node) {
        result.push_back(0); // null marker
        return result;
    }

    if (!node->left && !node->right) {
        result.push_back(1); // leaf marker
        result.push_back(node->data);
    } else {
        result.push_back(2); // internal marker
        auto left_data = serializeTree(node->left);
        auto right_data = serializeTree(node->right);
        result.insert(result.end(), left_data.begin(), left_data.end());
        result.insert(result.end(), right_data.begin(), right_data.end());
    }
    return result;
}

std::pair<std::shared_ptr<HuffmanNode>, size_t> deserializeTree(const std::vector<uint8_t>& data, size_t offset) {
    if (offset >= data.size()) {
        return {nullptr, offset};
    }

    uint8_t marker = data[offset++];
    if (marker == 0) {
        return {nullptr, offset};
    } else if (marker == 1) {
        // Leaf node
        if (offset >= data.size()) {
            throw std::runtime_error("Corrupted tree data");
        }
        uint8_t nodeData = data[offset++];
        auto node = std::make_shared<HuffmanNode>(nodeData, 0);
        return {node, offset};
    } else if (marker == 2) {
        // Internal node
        auto node = std::make_shared<HuffmanNode>(0);
        auto [left, newOffset] = deserializeTree(data, offset);
        auto [right, finalOffset] = deserializeTree(data, newOffset);
        node->left = left;
        node->right = right;
        return {node, finalOffset};
    } else {
        throw std::runtime_error("Invalid tree marker");
    }
}

#endif