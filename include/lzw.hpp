#ifndef LZW_HPP
#define LZW_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

class LZW {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        std::unordered_map<std::string, int> dictionary;
        for (int i = 0; i < 256; i++) {
            dictionary[std::string(1, static_cast<char>(i))] = i;
        }

        std::string current;
        std::vector<int> result;
        int dict_size = 256;

        for (uint8_t byte : data) {
            std::string next = current + static_cast<char>(byte);

            if (dictionary.find(next) != dictionary.end()) {
                current = next;
            } else {
                result.push_back(dictionary[current]);
                dictionary[next] = dict_size++;
                current = std::string(1, static_cast<char>(byte));
            }
        }

        if (!current.empty()) {
            result.push_back(dictionary[current]);
        }

        std::vector<uint8_t> compressed;
        for (int code : result) {
            compressed.push_back(static_cast<uint8_t>(code & 0xFF));
            compressed.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
        }
        return compressed;
    }
    
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
        if (data.size() % 2 != 0) return {};

        std::unordered_map<int, std::string> dictionary;
        for (int i = 0; i < 256; i++) {
            dictionary[i] = std::string(1, static_cast<char>(i));
        }

        std::vector<int> codes;
        for (size_t i = 0; i < data.size(); i += 2) {
            int code = data[i] | (data[i + 1] << 8);
            codes.push_back(code);
        }

        if (codes.empty()) {
            return {};
        }

        std::string current = dictionary[codes[0]];
        std::vector<uint8_t> result;

        for (char c : current) {
            result.push_back(static_cast<uint8_t>(c));
        }

        int dict_size = 256;

        for (size_t i = 1; i < codes.size(); i++) {
            int code = codes[i];
            std::string entry;

            if (dictionary.find(code) != dictionary.end()) {
                entry = dictionary[code];
            } else if (code == dict_size) {
                entry = current + current[0];
            } else {
                return {};
            }

            for (char c : entry) {
                result.push_back(static_cast<uint8_t>(c));
            }

            dictionary[dict_size++] = current + entry[0];
            current = entry;
        }
        return result;
    }
};

#endif