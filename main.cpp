#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include "argparse/argparse.hpp"
#include "include/rle.hpp"
#include "include/huffman.hpp"
#include "include/lzw.hpp"

std::vector<uint8_t> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    return std::vector<uint8_t>(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
}

void writeFile(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write to file: " + filename);
    }
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void writeUint64(std::vector<uint8_t>& data, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

uint64_t readUint64(const std::vector<uint8_t>& data, size_t offset) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("compress", "1.0");

    program.add_argument("-a", "--algorithm")
        .help("compression algorithm")
        .default_value(std::string("rle"))
        .choices("rle", "huffman", "lzw");

    program.add_argument("-d", "--decompress")
        .help("Decompress instead of compress")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-i", "--input")
        .help("input file")
        .required();

    program.add_argument("-o", "--output")
        .help("output file")
        .required();

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << "\n";
        std::cerr << program;
        return 1;
    }

    std::string algorithm = program.get<std::string>("algorithm");
    bool decompress = program.get<bool>("decompress");
    std::string input_file = program.get<std::string>("input");
    std::string output_file = program.get<std::string>("output");

    try {
        std::vector<uint8_t> data = readFile(input_file);
        std::vector<uint8_t> result;

        if (!decompress) {
            std::cout << "Original size: " << data.size() << " bytes\n";

            if (algorithm == "rle") {
                result = RLE::compress(data);
                std::cout << "Using RLE compression.\n";
            } else if (algorithm == "huffman") {
                std::cout << "Using Huffman encoding.\n";
                auto [compressed_data, tree] = Huffman::compress(data);

                std::unordered_map<uint8_t, std::string> codes;
                std::function<void(std::shared_ptr<HuffmanNode>, std::string)> generateCodes =
                    [&](std::shared_ptr<HuffmanNode> node, std::string code) {
                        if (!node) return;
                        if (!node->left && !node->right) {
                            codes[node->data] = code.empty() ? "0" : code;
                            return;
                        }
                        generateCodes(node->left, code + "0");
                        generateCodes(node->right, code + "1");
                    };
                generateCodes(tree, "");

                size_t original_bits = 0;
                for (uint8_t byte : data) {
                    original_bits += codes[byte].length();
                }

                auto tree_data = serializeTree(tree);
                result.clear();
                writeUint64(result, original_bits);
                writeUint64(result, tree_data.size());

                result.insert(result.end(), tree_data.begin(), tree_data.end());
                result.insert(result.end(), compressed_data.begin(), compressed_data.end());

            } else if (algorithm == "lzw") {
                result = LZW::compress(data);
                std::cout << "Using LZW compression.\n";
            }

            std::cout << "Compressed size: " << result.size() << " bytes\n";
            std::cout << "Compression ratio: " <<
                (data.empty() ? 0.0 : (100.0 * result.size() / data.size())) << "%\n";

        } else {
            std::cout << "Decompressing.\n";

            if (algorithm == "rle") {
                result = RLE::decompress(data);
            } else if (algorithm == "huffman") {
                if (data.size() < 16) {
                    throw std::runtime_error("Invalid Huffman compressed file format");
                }

                uint64_t original_bits = readUint64(data, 0);
                uint64_t tree_size = readUint64(data, 8);

                if (data.size() < 16 + tree_size) {
                    throw std::runtime_error("Corrupted Huffman compressed file");
                }

                std::vector<uint8_t> tree_data(data.begin() + 16, data.begin() + 16 + tree_size);
                auto [tree, _] = deserializeTree(tree_data, 0);

                std::vector<uint8_t> compressed_data(data.begin() + 16 + tree_size, data.end());
                result = Huffman::decompress(compressed_data, tree, original_bits);

            } else if (algorithm == "lzw") {
                result = LZW::decompress(data);
            }

            std::cout << "Decompressed size: " << result.size() << " bytes\n";
        }

        writeFile(output_file, result);
        std::cout << "Operation completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
