#include "ClientImageCache.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace {
constexpr size_t kMaxImageBytes = 6 * 1024 * 1024;
constexpr uint64_t kMaxImagePixels = 64ull * 1024ull * 1024ull;

bool startsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool isHttpUrl(const std::string& value) {
    return startsWith(value, "http://") || startsWith(value, "https://");
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string normalizeFilePath(std::string value) {
    if (startsWith(value, "file://")) {
        value.erase(0, 7);
        if (startsWith(value, "localhost/")) {
            value.erase(0, 9);
        }
    }

    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            auto hexValue = [](char digit) -> int {
                if (digit >= '0' && digit <= '9') return digit - '0';
                if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
                if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
                return -1;
            };
            int high = hexValue(value[i + 1]);
            int low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }
    return decoded;
}

bool readFileBytes(const std::string& path, std::vector<unsigned char>& bytes, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Could not open image file.";
        return false;
    }

    std::streamoff length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > kMaxImageBytes) {
        error = "Image file must be smaller than 6 MiB.";
        return false;
    }

    bytes.resize(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        error = "Could not read image file.";
        bytes.clear();
        return false;
    }
    return true;
}

std::string base64Encode(const std::vector<unsigned char>& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t block = static_cast<uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) block |= static_cast<uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) block |= bytes[i + 2];

        encoded.push_back(alphabet[(block >> 18) & 0x3f]);
        encoded.push_back(alphabet[(block >> 12) & 0x3f]);
        encoded.push_back(i + 1 < bytes.size() ? alphabet[(block >> 6) & 0x3f] : '=');
        encoded.push_back(i + 2 < bytes.size() ? alphabet[block & 0x3f] : '=');
    }
    return encoded;
}

bool base64Decode(const std::string& encoded, std::vector<unsigned char>& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t accumulator = 0;
    int bits = -8;
    bool paddingStarted = false;

    for (unsigned char ch : encoded) {
        if (std::isspace(ch)) continue;
        if (ch == '=') {
            paddingStarted = true;
            continue;
        }
        if (paddingStarted) return false;

        const char* found = std::find(std::begin(alphabet), std::end(alphabet) - 1, static_cast<char>(ch));
        if (found == std::end(alphabet) - 1) return false;
        accumulator = (accumulator << 6) | static_cast<int>(found - alphabet);
        bits += 6;
        if (bits >= 0) {
            bytes.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xff));
            bits -= 8;
            if (bytes.size() > kMaxImageBytes) return false;
        }
    }
    return !bytes.empty();
}

std::string imageMimeType(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "webp") return "image/webp";
    if (extension == "bmp") return "image/bmp";
    if (extension == "tga") return "image/x-tga";
    return "image/png";
}

bool validateImageBytes(const std::vector<unsigned char>& bytes, int& width, int& height, std::string& error) {
    int channels = 0;
    if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels)) {
        error = "File is not a supported image.";
        return false;
    }
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > kMaxImagePixels) {
        error = "Image dimensions are too large.";
        return false;
    }
    return true;
}
} // namespace

bool ClientImageCache::initializeUploadIcon(const std::string& path) {
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &uploadIconWidth_, &uploadIconHeight_, &channels, 4);
    if (!pixels) {
        std::cerr << "[WARNING] Could not load " << path << ": " << stbi_failure_reason() << "\n";
        return false;
    }
    uploadIconTexture_ = createTexture(pixels, uploadIconWidth_, uploadIconHeight_);
    stbi_image_free(pixels);
    return uploadIconTexture_ != 0;
}

void ClientImageCache::loadForBoard(const BoardState& board) {
    processLoadTasks();

    std::unordered_set<std::string> usedSources;
    for (const auto& [id, card] : board.cards) {
        if (card.mediaURL.empty()) continue;
        usedSources.insert(card.mediaURL);
        auto imageIt = images_.find(card.mediaURL);
        if (imageIt == images_.end() ||
            (!imageIt->second.loading && imageIt->second.texture == 0 && !imageIt->second.failed)) {
            scheduleLoad(card.mediaURL);
        }
    }

    for (auto imageIt = images_.begin(); imageIt != images_.end();) {
        if (usedSources.find(imageIt->first) == usedSources.end() && !imageIt->second.loading) {
            if (imageIt->second.texture != 0) glDeleteTextures(1, &imageIt->second.texture);
            imageIt = images_.erase(imageIt);
        } else {
            ++imageIt;
        }
    }
}

void ClientImageCache::shutdown() {
    for (std::future<LoadResult>& task : loadTasks_) {
        if (task.valid()) task.wait();
    }
    loadTasks_.clear();

    for (auto& [source, image] : images_) {
        if (image.texture != 0) glDeleteTextures(1, &image.texture);
    }
    images_.clear();

    if (uploadIconTexture_ != 0) {
        glDeleteTextures(1, &uploadIconTexture_);
        uploadIconTexture_ = 0;
    }
    uploadIconWidth_ = 0;
    uploadIconHeight_ = 0;
}

const ClientImageCache::Image* ClientImageCache::find(const std::string& source) const {
    auto it = images_.find(source);
    return it == images_.end() ? nullptr : &it->second;
}

std::string ClientImageCache::sourceFromDrop(const std::string& source, std::string& error) {
    if (isHttpUrl(source) || startsWith(source, "data:image/")) return source;

    const std::string path = normalizeFilePath(source);
    std::vector<unsigned char> bytes;
    int width = 0;
    int height = 0;
    if (!readFileBytes(path, bytes, error) || !validateImageBytes(bytes, width, height, error)) return "";

    return "data:" + imageMimeType(path) + ";base64," + base64Encode(bytes);
}

void ClientImageCache::scheduleLoad(const std::string& source) {
    Image& image = images_[source];
    if (image.loading || image.texture != 0 || image.failed || loadTasks_.size() >= 4) return;

    image.loading = true;
    try {
        loadTasks_.emplace_back(std::async(std::launch::async, [source]() {
            try {
                return loadImageSource(source);
            } catch (const std::exception& exception) {
                LoadResult result;
                result.source = source;
                result.error = exception.what();
                return result;
            }
        }));
    } catch (const std::exception& exception) {
        image.loading = false;
        image.failed = true;
        std::cerr << "[WARNING] Could not start image loader: " << exception.what() << "\n";
    }
}

void ClientImageCache::processLoadTasks() {
    for (size_t i = 0; i < loadTasks_.size();) {
        std::future<LoadResult>& task = loadTasks_[i];
        if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }

        LoadResult result;
        try {
            result = task.get();
        } catch (const std::exception& exception) {
            std::cerr << "[WARNING] Image loading failed: " << exception.what() << "\n";
        }

        auto imageIt = images_.find(result.source);
        if (imageIt != images_.end()) {
            Image& image = imageIt->second;
            image.loading = false;
            if (!result.pixels.empty()) {
                image.texture = createTexture(result.pixels.data(), result.width, result.height);
                image.width = result.width;
                image.height = result.height;
                image.failed = image.texture == 0;
            } else {
                image.failed = true;
                if (!result.error.empty()) std::cerr << "[WARNING] Could not load card image: " << result.error << "\n";
            }
        }

        loadTasks_.erase(loadTasks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

GLuint ClientImageCache::createTexture(const unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    return texture;
}

ClientImageCache::LoadResult ClientImageCache::loadImageSource(const std::string& source) {
    LoadResult result;
    result.source = source;
    std::vector<unsigned char> bytes;

    if (startsWith(source, "data:image/")) {
        const size_t comma = source.find(',');
        if (comma == std::string::npos || source.substr(0, comma).find(";base64") == std::string::npos ||
            !base64Decode(source.substr(comma + 1), bytes)) {
            result.error = "Image data URL is invalid or too large.";
            return result;
        }
    } else if (isHttpUrl(source)) {
        const std::string command = "curl --location --fail --silent --show-error --proto '=http,https' "
            "--proto-redir '=http,https' --max-time 20 --max-filesize " +
            std::to_string(kMaxImageBytes) + " -- " + shellQuote(source);
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            result.error = "Could not start curl to download the image.";
            return result;
        }

        std::array<unsigned char, 8192> chunk{};
        bool tooLarge = false;
        size_t totalBytes = 0;
        while (true) {
            const size_t count = fread(chunk.data(), 1, chunk.size(), pipe);
            if (count == 0) break;
            if (totalBytes + count > kMaxImageBytes) {
                tooLarge = true;
            } else if (!tooLarge) {
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
            }
            totalBytes += count;
        }
        const int status = pclose(pipe);
        if (tooLarge) {
            result.error = "Downloaded image exceeds the 6 MiB limit.";
            return result;
        }
        if (status != 0) {
            result.error = "Could not download the image URL.";
            return result;
        }
    } else {
        const std::string path = normalizeFilePath(source);
        if (!readFileBytes(path, bytes, result.error)) return result;
    }

    if (!validateImageBytes(bytes, result.width, result.height, result.error)) return result;

    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &result.width, &result.height, &channels, 4);
    if (!pixels) {
        result.error = stbi_failure_reason() ? stbi_failure_reason() : "Image decoding failed.";
        return result;
    }
    const size_t pixelBytes = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
    result.pixels.assign(pixels, pixels + pixelBytes);
    stbi_image_free(pixels);
    return result;
}
