#ifndef CLIENT_IMAGE_CACHE_H
#define CLIENT_IMAGE_CACHE_H

#include "BoardStructs.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

// Owns decoded image data and GPU textures used by card rendering.
class ClientImageCache {
public:
    struct Image {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        bool loading = false;
        bool failed = false;
    };

    bool initializeUploadIcon(const std::string& path);
    void loadForBoard(const BoardState& board);
    void shutdown();

    const Image* find(const std::string& source) const;
    GLuint uploadIconTexture() const { return uploadIconTexture_; }
    int uploadIconWidth() const { return uploadIconWidth_; }
    int uploadIconHeight() const { return uploadIconHeight_; }

    // URLs and data URLs pass through. Local files are validated and embedded
    // so the resulting card source does not depend on a machine-specific path.
    static std::string sourceFromDrop(const std::string& source, std::string& error);

private:
    struct LoadResult {
        std::string source;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        std::string error;
    };

    std::unordered_map<std::string, Image> images_;
    std::vector<std::future<LoadResult>> loadTasks_;
    GLuint uploadIconTexture_ = 0;
    int uploadIconWidth_ = 0;
    int uploadIconHeight_ = 0;

    void scheduleLoad(const std::string& source);
    void processLoadTasks();
    GLuint createTexture(const unsigned char* pixels, int width, int height);
    static LoadResult loadImageSource(const std::string& source);
};

#endif
