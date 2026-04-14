#ifndef SSIM_CALCULATOR_H
#define SSIM_CALCULATOR_H

#include <vector>
#include <cstdint>
#include <cmath>

namespace sdk {
namespace video {
namespace testing {

class SSIMCalculator {
public:
    static double calculateSSIM(const uint8_t* img1, const uint8_t* img2, int width, int height) {
        if (!img1 || !img2 || width <= 0 || height <= 0) return 0.0;

        const double C1 = 6.5025;  // (0.01 * 255)^2
        const double C2 = 58.5225; // (0.03 * 255)^2

        int numPixels = width * height;

        // Use double for precision, accumulate for RGB channels (ignore Alpha for SSIM here)
        double mean1 = 0.0;
        double mean2 = 0.0;

        for (int i = 0; i < numPixels; ++i) {
            // Using luminance (grayscale) for SSIM, simple average or weighted.
            // Using Rec. 601 Luma: Y = 0.299 R + 0.587 G + 0.114 B
            int idx = i * 4;
            double y1 = 0.299 * img1[idx] + 0.587 * img1[idx+1] + 0.114 * img1[idx+2];
            double y2 = 0.299 * img2[idx] + 0.587 * img2[idx+1] + 0.114 * img2[idx+2];

            mean1 += y1;
            mean2 += y2;
        }

        mean1 /= numPixels;
        mean2 /= numPixels;

        double var1 = 0.0;
        double var2 = 0.0;
        double covar = 0.0;

        for (int i = 0; i < numPixels; ++i) {
            int idx = i * 4;
            double y1 = 0.299 * img1[idx] + 0.587 * img1[idx+1] + 0.114 * img1[idx+2];
            double y2 = 0.299 * img2[idx] + 0.587 * img2[idx+1] + 0.114 * img2[idx+2];

            double diff1 = y1 - mean1;
            double diff2 = y2 - mean2;

            var1 += diff1 * diff1;
            var2 += diff2 * diff2;
            covar += diff1 * diff2;
        }

        var1 /= numPixels;
        var2 /= numPixels;
        covar /= numPixels;

        double numerator = (2 * mean1 * mean2 + C1) * (2 * covar + C2);
        double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

        return numerator / denominator;
    }
};

} // namespace testing
} // namespace video
} // namespace sdk

#endif // SSIM_CALCULATOR_H
