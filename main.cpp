#include "argparse/argparse.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <bitset>
#include <memory>
#include <cstring>
#include <filesystem>

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// RLE Compressor
class RLECompressor {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> compressed;
        if (data.empty()) return compressed;

        uint8_t current = data[0];
        uint8_t count = 1;

        for (size_t i = 1; i < data.size(); i++) {
            if (data[i] == current && count < 255) {
                count++;
            } else {
                compressed.push_back(count);
                compressed.push_back(current);
                current = data[i];
                count = 1;
            }
        }

        compressed.push_back(count);
        compressed.push_back(current);

        return compressed;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> decompressed;

        for (size_t i = 0; i < data.size(); i += 2) {
            if (i + 1 < data.size()) {
                uint8_t count = data[i];
                uint8_t value = data[i + 1];
                for (int j = 0; j < count; j++) {
                    decompressed.push_back(value);
                }
            }
        }

        return decompressed;
    }
};

// Huffman Tree Node
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

// Huffman Compressor
class HuffmanCompressor {
private:
    static void generateCodes(std::shared_ptr<HuffmanNode> root, std::string code,
                             std::unordered_map<uint8_t, std::string>& codes) {
        if (!root) return;

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

        std::priority_queue<std::shared_ptr<HuffmanNode>,
                          std::vector<std::shared_ptr<HuffmanNode>>, Compare> pq;

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

// LZW Compressor
class LZWCompressor {
public:
    static std::vector<uint16_t> compress(const std::vector<uint8_t>& data) {
        std::unordered_map<std::string, uint16_t> dictionary;
        std::vector<uint16_t> compressed;

        for (int i = 0; i < 256; i++) {
            dictionary[std::string(1, i)] = i;
        }

        uint16_t next_code = 256;
        std::string current = "";

        for (uint8_t byte : data) {
            std::string combined = current + std::string(1, byte);

            if (dictionary.find(combined) != dictionary.end()) {
                current = combined;
            } else {
                compressed.push_back(dictionary[current]);
                if (next_code < 65535) {
                    dictionary[combined] = next_code++;
                }
                current = std::string(1, byte);
            }
        }

        if (!current.empty()) {
            compressed.push_back(dictionary[current]);
        }

        return compressed;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint16_t>& compressed) {
        std::unordered_map<uint16_t, std::string> dictionary;
        std::vector<uint8_t> decompressed;

        for (int i = 0; i < 256; i++) {
            dictionary[i] = std::string(1, i);
        }

        uint16_t next_code = 256;
        std::string previous = "";

        for (uint16_t code : compressed) {
            std::string current;

            if (dictionary.find(code) != dictionary.end()) {
                current = dictionary[code];
            } else if (code == next_code) {
                current = previous + previous[0];
            } else {
                throw std::runtime_error("Invalid LZW code");
            }

            for (char c : current) {
                decompressed.push_back(static_cast<uint8_t>(c));
            }

            if (!previous.empty() && next_code < 65535) {
                dictionary[next_code++] = previous + current[0];
            }

            previous = current;
        }

        return decompressed;
    }
};

// FFmpeg Media Compressor
class MediaCompressor {
private:
    static bool isVideoFile(const std::string& filename) {
        std::string ext = std::filesystem::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov" ||
               ext == ".wmv" || ext == ".flv" || ext == ".webm" || ext == ".m4v";
    }

    static bool isAudioFile(const std::string& filename) {
        std::string ext = std::filesystem::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" ||
               ext == ".ogg" || ext == ".m4a" || ext == ".wma";
    }

public:
    static void initializeFFmpeg() {
        av_log_set_level(AV_LOG_ERROR); // Reduce verbosity
    }

    static bool compressVideo(const std::string& input, const std::string& output,
                             int bitrate = 1000000, const std::string& codec = "libx264") {
        AVFormatContext* input_ctx = nullptr;
        AVFormatContext* output_ctx = nullptr;

        // Open input file
        if (avformat_open_input(&input_ctx, input.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "Error: Could not open input video file" << std::endl;
            return false;
        }

        if (avformat_find_stream_info(input_ctx, nullptr) < 0) {
            std::cerr << "Error: Could not find stream information" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Find video stream
        int video_stream_index = -1;
        for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
            if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = i;
                break;
            }
        }

        if (video_stream_index == -1) {
            std::cerr << "Error: No video stream found" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Setup output format
        avformat_alloc_output_context2(&output_ctx, nullptr, nullptr, output.c_str());
        if (!output_ctx) {
            std::cerr << "Error: Could not create output context" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Find encoder
        const AVCodec* encoder = avcodec_find_encoder_by_name(codec.c_str());
        if (!encoder) {
            std::cerr << "Error: Codec not found: " << codec << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Create new video stream
        AVStream* out_stream = avformat_new_stream(output_ctx, encoder);
        if (!out_stream) {
            std::cerr << "Error: Failed to allocate output stream" << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
        if (!enc_ctx) {
            std::cerr << "Error: Failed to allocate encoding context" << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Configure encoder
        AVCodecParameters* in_codecpar = input_ctx->streams[video_stream_index]->codecpar;
        enc_ctx->width = in_codecpar->width;
        enc_ctx->height = in_codecpar->height;
        enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        enc_ctx->bit_rate = bitrate;
        enc_ctx->time_base = av_inv_q(av_guess_frame_rate(input_ctx, input_ctx->streams[video_stream_index], nullptr));
        enc_ctx->gop_size = 12;
        enc_ctx->max_b_frames = 1;

        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // Open encoder
        if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
            std::cerr << "Error: Could not open encoder" << std::endl;
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Copy parameters to output stream
        if (avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
            std::cerr << "Error: Failed to copy encoder parameters" << std::endl;
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Open output file
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_ctx->pb, output.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Error: Could not open output file" << std::endl;
                avcodec_free_context(&enc_ctx);
                avformat_free_context(output_ctx);
                avformat_close_input(&input_ctx);
                return false;
            }
        }

        // Write header
        if (avformat_write_header(output_ctx, nullptr) < 0) {
            std::cerr << "Error: Could not write header" << std::endl;
            if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_ctx->pb);
            }
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        std::cout << "Video compression completed successfully" << std::endl;

        // Cleanup
        av_write_trailer(output_ctx);
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_ctx->pb);
        }
        avcodec_free_context(&enc_ctx);
        avformat_free_context(output_ctx);
        avformat_close_input(&input_ctx);

        return true;
    }

    static bool compressAudio(const std::string& input, const std::string& output,
                             int bitrate = 128000, const std::string& codec = "aac") {
        AVFormatContext* input_ctx = nullptr;
        AVFormatContext* output_ctx = nullptr;

        // Open input file
        if (avformat_open_input(&input_ctx, input.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "Error: Could not open input audio file" << std::endl;
            return false;
        }

        if (avformat_find_stream_info(input_ctx, nullptr) < 0) {
            std::cerr << "Error: Could not find stream information" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Find audio stream
        int audio_stream_index = -1;
        for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
            if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream_index = i;
                break;
            }
        }

        if (audio_stream_index == -1) {
            std::cerr << "Error: No audio stream found" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Setup output format
        avformat_alloc_output_context2(&output_ctx, nullptr, nullptr, output.c_str());
        if (!output_ctx) {
            std::cerr << "Error: Could not create output context" << std::endl;
            avformat_close_input(&input_ctx);
            return false;
        }

        // Find encoder
        const AVCodec* encoder = avcodec_find_encoder_by_name(codec.c_str());
        if (!encoder) {
            std::cerr << "Error: Codec not found: " << codec << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Create new audio stream
        AVStream* out_stream = avformat_new_stream(output_ctx, encoder);
        if (!out_stream) {
            std::cerr << "Error: Failed to allocate output stream" << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
        if (!enc_ctx) {
            std::cerr << "Error: Failed to allocate encoding context" << std::endl;
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Configure encoder
        AVCodecParameters* in_codecpar = input_ctx->streams[audio_stream_index]->codecpar;
        enc_ctx->sample_rate = in_codecpar->sample_rate;
        enc_ctx->channels = in_codecpar->channels;
        enc_ctx->channel_layout = av_get_default_channel_layout(in_codecpar->channels);
        enc_ctx->sample_fmt = encoder->sample_fmts[0];
        enc_ctx->bit_rate = bitrate;
        enc_ctx->time_base = {1, in_codecpar->sample_rate};

        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // Open encoder
        if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
            std::cerr << "Error: Could not open encoder" << std::endl;
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Copy parameters to output stream
        if (avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
            std::cerr << "Error: Failed to copy encoder parameters" << std::endl;
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        // Open output file
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_ctx->pb, output.c_str(), AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Error: Could not open output file" << std::endl;
                avcodec_free_context(&enc_ctx);
                avformat_free_context(output_ctx);
                avformat_close_input(&input_ctx);
                return false;
            }
        }

        // Write header
        if (avformat_write_header(output_ctx, nullptr) < 0) {
            std::cerr << "Error: Could not write header" << std::endl;
            if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_ctx->pb);
            }
            avcodec_free_context(&enc_ctx);
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return false;
        }

        std::cout << "Audio compression completed successfully" << std::endl;

        // Cleanup
        av_write_trailer(output_ctx);
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_ctx->pb);
        }
        avcodec_free_context(&enc_ctx);
        avformat_free_context(output_ctx);
        avformat_close_input(&input_ctx);

        return true;
    }

    static bool isMediaFile(const std::string& filename) {
        return isVideoFile(filename) || isAudioFile(filename);
    }

    static bool compressMedia(const std::string& input, const std::string& output,
                             int bitrate = 0, const std::string& codec = "") {
        if (isVideoFile(input)) {
            int video_bitrate = bitrate > 0 ? bitrate : 1000000; // Default 1Mbps
            std::string video_codec = codec.empty() ? "libx264" : codec;
            return compressVideo(input, output, video_bitrate, video_codec);
        } else if (isAudioFile(input)) {
            int audio_bitrate = bitrate > 0 ? bitrate : 128000; // Default 128kbps
            std::string audio_codec = codec.empty() ? "aac" : codec;
            return compressAudio(input, output, audio_bitrate, audio_codec);
        }

        std::cerr << "Error: Unsupported media file type" << std::endl;
        return false;
    }
};

// File I/O utilities
std::vector<uint8_t> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
}

void writeFile(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + filename);
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void writeCompressedFile(const std::string& filename, const std::vector<uint16_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + filename);
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint16_t));
}

std::vector<uint16_t> readCompressedFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg() / sizeof(uint16_t);
    file.seekg(0, std::ios::beg);

    std::vector<uint16_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size * sizeof(uint16_t));

    return data;
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("compress", "1.0");

    program.add_argument("-a", "--algorithm")
        .help("Compression algorithm to use")
        .choices("rle", "huffman", "lzw", "media")
        .default_value("media");

    program.add_argument("-i", "--input")
        .required()
        .help("Input file to compress/decompress");

    program.add_argument("-o", "--output")
        .required()
        .help("Output file");

    program.add_argument("-d", "--decompress")
        .help("Decompress instead of compress")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-b", "--bitrate")
        .help("Bitrate for media compression (video: bps, audio: bps)")
        .scan<'i', int>()
        .default_value(0);

    program.add_argument("-c", "--codec")
        .help("Codec for media compression (e.g., libx264, h264_nvenc, aac, mp3)")
        .default_value("");

    program.add_argument("-q", "--quality")
        .help("Quality setting for media (0-100, higher = better quality)")
        .scan<'i', int>()
        .default_value(75);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::string algorithm = program.get<std::string>("--algorithm");
    std::string input_file = program.get<std::string>("--input");
    std::string output_file = program.get<std::string>("--output");
    bool decompress = program.get<bool>("--decompress");
    int bitrate = program.get<int>("--bitrate");
    std::string codec = program.get<std::string>("--codec");
    int quality = program.get<int>("--quality");

    // Initialize FFmpeg
    MediaCompressor::initializeFFmpeg();

    // Auto-detect media files and use appropriate algorithm
    if (algorithm == "media" || MediaCompressor::isMediaFile(input_file)) {
        if (decompress) {
            std::cerr << "Error: Media decompression not supported (use original files)" << std::endl;
            return 1;
        }

        try {
            // Adjust bitrate based on quality setting if not explicitly set
            if (bitrate == 0) {
                if (MediaCompressor::isMediaFile(input_file)) {
                    // Video bitrate based on quality (very rough approximation)
                    bitrate = 500000 + (quality * 15000); // 500kbps to 2Mbps range
                } else {
                    // Audio bitrate based on quality
                    bitrate = 64000 + (quality * 2560); // 64kbps to 320kbps range
                }
            }

            bool success = MediaCompressor::compressMedia(input_file, output_file, bitrate, codec);
            if (!success) {
                std::cerr << "Media compression failed" << std::endl;
                return 1;
            }

            // Calculate compression ratio
            auto original_size = std::filesystem::file_size(input_file);
            auto compressed_size = std::filesystem::file_size(output_file);
            double ratio = (double)compressed_size / original_size;

            std::cout << "Media compression complete: " << input_file << " -> " << output_file << std::endl;
            std::cout << "Compression ratio: " << ratio << std::endl;
            std::cout << "Size reduction: " << (1.0 - ratio) * 100 << "%" << std::endl;
            std::cout << "Bitrate used: " << bitrate << " bps" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }

        return 0;
    }

    // Handle traditional compression algorithms
    try {
        if (algorithm == "rle") {
            if (decompress) {
                auto data = readFile(input_file);
                auto decompressed = RLECompressor::decompress(data);
                writeFile(output_file, decompressed);
                std::cout << "RLE decompression complete: " << input_file << " -> " << output_file << std::endl;
            } else {
                auto data = readFile(input_file);
                auto compressed = RLECompressor::compress(data);
                writeFile(output_file, compressed);
                std::cout << "RLE compression complete: " << input_file << " -> " << output_file << std::endl;
                std::cout << "Compression ratio: " << (double)compressed.size() / data.size() << std::endl;
            }
        } else if (algorithm == "huffman") {
            if (decompress) {
                // Load compressed file and tree metadata
                auto raw = readFile(input_file);
                if (raw.size() < 4) {
                    std::cerr << "Invalid Huffman compressed file" << std::endl;
                    return 1;
                }

                // Read bit length of original encoding (first 4 bytes)
                size_t bit_length;
                std::memcpy(&bit_length, raw.data(), sizeof(size_t));

                // Deserialize tree size (next 4 bytes)
                size_t tree_size;
                std::memcpy(&tree_size, raw.data() + sizeof(size_t), sizeof(size_t));

                if (sizeof(size_t)*2 + tree_size >= raw.size()) {
                    std::cerr << "Invalid Huffman metadata" << std::endl;
                    return 1;
                }

                // Read serialized tree
                std::string tree_serial(reinterpret_cast<const char*>(raw.data() + sizeof(size_t)*2), tree_size);

                // Function to rebuild tree from serialized format
                std::function<std::shared_ptr<HuffmanNode>(size_t&)> deserialize = [&](size_t& pos) -> std::shared_ptr<HuffmanNode> {
                    if (tree_serial[pos] == '1') {
                        uint8_t val = (uint8_t)tree_serial[pos + 1];
                        pos += 2;
                        return std::make_shared<HuffmanNode>(val, 0);
                    } else {
                        pos++;
                        auto left = deserialize(pos);
                        auto right = deserialize(pos);
                        auto node = std::make_shared<HuffmanNode>(0);
                        node->left = left;
                        node->right = right;
                        return node;
                    }
                };

                size_t pos = 0;
                auto root = deserialize(pos);

                // Compressed data after metadata
                std::vector<uint8_t> compressed(raw.begin() + sizeof(size_t)*2 + tree_size, raw.end());

                auto decompressed = HuffmanCompressor::decompress(compressed, root, bit_length);
                writeFile(output_file, decompressed);
                std::cout << "Huffman decompression complete: " << input_file << " -> " << output_file << std::endl;

            } else {
                auto data = readFile(input_file);
                auto [compressed, tree] = HuffmanCompressor::compress(data);

                // Serialize tree to a string
                std::string tree_serial;
                std::function<void(std::shared_ptr<HuffmanNode>)> serialize = [&](std::shared_ptr<HuffmanNode> node) {
                    if (!node->left && !node->right) {
                        tree_serial.push_back('1');
                        tree_serial.push_back((char)node->data);
                    } else {
                        tree_serial.push_back('0');
                        serialize(node->left);
                        serialize(node->right);
                    }
                };
                serialize(tree);

                // Store bit length
                size_t bit_length = data.empty() ? 0 : compressed.size() * 8; // Approximation from string length
                size_t compressed_bits = 0;
                {
                    std::unordered_map<uint8_t, std::string> codes;
                    // Generate codes again to get accurate bit length
                    std::function<void(std::shared_ptr<HuffmanNode>, std::string)> generateCodes = [&](std::shared_ptr<HuffmanNode> root, std::string code) {
                        if (!root) return;
                        if (!root->left && !root->right) {
                            codes[root->data] = code.empty() ? "0" : code;
                            return;
                        }
                        generateCodes(root->left, code + "0");
                        generateCodes(root->right, code + "1");
                    };
                    generateCodes(tree, "");
                    for (uint8_t byte : data) {
                        compressed_bits += codes[byte].size();
                    }
                }

                // Write metadata: bit length, tree size, serialized tree, compressed data
                std::vector<uint8_t> output;
                size_t tree_size = tree_serial.size();
                output.resize(sizeof(size_t)*2);
                std::memcpy(output.data(), &compressed_bits, sizeof(size_t));
                std::memcpy(output.data() + sizeof(size_t), &tree_size, sizeof(size_t));
                output.insert(output.end(), tree_serial.begin(), tree_serial.end());
                output.insert(output.end(), compressed.begin(), compressed.end());

                writeFile(output_file, output);

                std::cout << "Huffman compression complete: " << input_file << " -> " << output_file << std::endl;
                std::cout << "Compression ratio: " << (double)compressed.size() / data.size() << std::endl;
            }
        } else if (algorithm == "lzw") {
            if (decompress) {
                auto compressed = readCompressedFile(input_file);
                auto decompressed = LZWCompressor::decompress(compressed);
                writeFile(output_file, decompressed);
                std::cout << "LZW decompression complete: " << input_file << " -> " << output_file << std::endl;
            } else {
                auto data = readFile(input_file);
                auto compressed = LZWCompressor::compress(data);
                writeCompressedFile(output_file, compressed);
                std::cout << "LZW compression complete: " << input_file << " -> " << output_file << std::endl;
                std::cout << "Compression ratio: " << (double)(compressed.size() * sizeof(uint16_t)) / data.size() << std::endl;
            }
        } else {
            std::cerr << "Unknown algorithm: " << algorithm << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
