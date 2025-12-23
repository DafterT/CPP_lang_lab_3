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

using ProcessFn = std::vector<unsigned char> (ImageConvolver::*)(const unsigned char*, int, int);

bool process_and_save(ImageConvolver& convolver,
                      const std::string& input_path,
                      const std::string& output_path,
                      ProcessFn process) {
    int w = 0;
    int h = 0;
    int channels = 0;

    unsigned char* img = convolver.loadImage(input_path.c_str(), w, h, channels);
    if (!img) {
        std::cerr << "Failed to load image: " << input_path << std::endl;
        return false;
    }

    std::vector<unsigned char> out = (convolver.*process)(img, w, h);
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

    bool ok = true;
    ok &= process_and_save(convolver, input_path, "img_blur_default.jpg",
                           &ImageConvolver::process_default);
    ok &= process_and_save(convolver, input_path, "img_blur_simd.jpg",
                           &ImageConvolver::process_SIMD);
    ok &= process_and_save(convolver, input_path, "img_blur_thread_pool.jpg",
                           &ImageConvolver::process_thread_pool);
    ok &= process_and_save(convolver, input_path, "img_blur_thread_pool_full.jpg",
                           &ImageConvolver::process_thread_pool_full);

    return ok ? 0 : 1;
}
