#ifndef RLE_HPP
#define RLE_HPP

#include <vector>
#include <cstdint>

class RLE {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> compressed;
        if (data.empty()) {
            return compressed;
        }
        for (size_t i = 0; i < data.size(); ) {
            uint8_t current = data[i];
            size_t count = 1;

            while ((i + count < data.size()) and (data[i + count] == current) and (count < 255)) {
                count++;
            }

            compressed.push_back(static_cast<uint8_t>(count));
            compressed.push_back(current);
            i += count;
        }
        return compressed;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> decompressed;

        for (size_t i = 0; i < data.size(); i += 2) {
            if (i + 1 >= data.size()) {
                break;
            }
            uint8_t count = data[i];
            uint8_t value = data[i + 1];
            for (int j = 0; j < count; j++) {
                decompressed.push_back(value);
            }
        }
        return decompressed;
    }
};

#endif