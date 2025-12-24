#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "image_convolver.h"
#include "stb_image.h"

namespace {

std::vector<float> gaussian_kernel_3x3() {
    return {
        1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
        1.0f / 9.0f, 10.0f / 9.0f, 1.0f / 9.0f,
        1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f
    };
    // 3x3 Gaussian kernel: [1 2 1; 2 4 2; 1 2 1] / 16
    return {
        1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
        2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
        1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f
    };
}

using ProcessFn = std::function<std::vector<unsigned char>(ImageConvolver&, const unsigned char*, int, int)>;

bool file_exists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

bool create_test_image(const std::string& output_path, ImageConvolver& convolver, int w, int h) {
    if (w <= 0 || h <= 0) {
        std::cerr << "Invalid test image size." << std::endl;
        return false;
    }

    std::vector<unsigned char> img(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    const int w_den = (w > 1) ? (w - 1) : 1;
    const int h_den = (h > 1) ? (h - 1) : 1;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            img[idx + 0] = static_cast<unsigned char>((x * 255) / w_den);
            img[idx + 1] = static_cast<unsigned char>((y * 255) / h_den);
            img[idx + 2] = static_cast<unsigned char>((x + y) % 256);
            img[idx + 3] = 255;
        }
    }

    if (!convolver.saveImage(output_path.c_str(), w, h, img.data())) {
        std::cerr << "Failed to create test image: " << output_path << std::endl;
        return false;
    }

    std::cout << "Created test image: " << output_path << std::endl;
    return true;
}

bool ensure_input_image(const std::string& input_path, ImageConvolver& convolver) {
    if (file_exists(input_path)) {
        return true;
    }

    std::cerr << "Input image not found, generating: " << input_path << std::endl;
    return create_test_image(input_path, convolver, 256, 256);
}

bool process_and_save(ImageConvolver& convolver,
                      const std::string& input_path,
                      const std::string& output_path,
                      const ProcessFn& process) {
    int w = 0;
    int h = 0;
    int channels = 0;

    unsigned char* img = convolver.loadImage(input_path.c_str(), w, h, channels);
    if (!img) {
        std::cerr << "Failed to load image: " << input_path << std::endl;
        return false;
    }

    std::vector<unsigned char> out = process(convolver, img, w, h);
    stbi_image_free(img);

    if (out.empty()) {
        std::cerr << "Processing returned empty result for: " << output_path << std::endl;
        return false;
    }

    if (!convolver.saveImage(output_path.c_str(), w, h, out.data())) {
        std::cerr << "Failed to save image: " << output_path << std::endl;
        return false;
    }

    std::cout << "Saved: " << output_path << std::endl;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::string input_path = (argc > 1) ? argv[1] : "img.jpg";

    const std::vector<float> kernel = gaussian_kernel_3x3();
    ImageConvolver convolver(kernel, 3, 3);

    if (!ensure_input_image(input_path, convolver)) {
        return 1;
    }

    bool ok = true;
    ok &= process_and_save(convolver, input_path, "img_blur_default.jpg",
                           [](ImageConvolver& c, const unsigned char* img, int w, int h) {
                               return c.process_default(img, w, h);
                           });
    ok &= process_and_save(convolver, input_path, "img_blur_simd.jpg",
                           [](ImageConvolver& c, const unsigned char* img, int w, int h) {
                               return c.process_SIMD(img, w, h);
                           });
    ok &= process_and_save(convolver, input_path, "img_blur_thread_pool.jpg",
                           [](ImageConvolver& c, const unsigned char* img, int w, int h) {
                               return c.process_thread_pool(img, w, h, 0);
                           });
    ok &= process_and_save(convolver, input_path, "img_blur_thread_pool_full.jpg",
                           [](ImageConvolver& c, const unsigned char* img, int w, int h) {
                               return c.process_thread_pool_full(img, w, h, 0);
                           });

    return ok ? 0 : 1;
}
