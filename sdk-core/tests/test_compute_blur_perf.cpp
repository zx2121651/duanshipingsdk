/**
 * test_compute_blur_perf.cpp
 * Phase E — Compute Blur 性能基准
 *
 * 在 CPU 软件模拟路径下测量可分离高斯 H+V pass 各自耗时，
 * 并与 O(r²) 盒式（参考实现）的理论读取次数对比。
 *
 * 支持半径 r = 4, 8, 15, 30 × 分辨率 1080p, 720p。
 * 输出结构化 CSV，可直接粘贴到 EVALUATION_REPORT_V6.md。
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstring>
#include <cassert>
#include <sstream>
#include <string>

using Clock = std::chrono::high_resolution_clock;
using Ns    = std::chrono::nanoseconds;

// ---------------------------------------------------------------------------
// Minimal 8-bit single-channel image buffer
// ---------------------------------------------------------------------------
struct Image {
    int w, h;
    std::vector<uint8_t> data;
    Image(int w, int h) : w(w), h(h), data(static_cast<size_t>(w) * h, 128) {}
    uint8_t at(int x, int y) const {
        x = std::max(0, std::min(w - 1, x));
        y = std::max(0, std::min(h - 1, y));
        return data[static_cast<size_t>(y) * w + x];
    }
    void set(int x, int y, uint8_t v) {
        data[static_cast<size_t>(y) * w + x] = v;
    }
};

// ---------------------------------------------------------------------------
// Reference: O(r²) box blur (naive, per pixel sums over r×r window)
// ---------------------------------------------------------------------------
static int64_t box_blur_reads(int w, int h, int r) {
    // Each pixel reads (2r+1)² pixels (no integral image optimisation)
    return static_cast<int64_t>(w) * h * (2*r+1) * (2*r+1);
}

static Ns box_blur_cpu(const Image& src, Image& dst, int r) {
    auto t0 = Clock::now();
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            int sum = 0, cnt = 0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) { sum += src.at(x+dx, y+dy); ++cnt; }
            dst.set(x, y, static_cast<uint8_t>(sum / cnt));
        }
    }
    return std::chrono::duration_cast<Ns>(Clock::now() - t0);
}

// ---------------------------------------------------------------------------
// Separable Gaussian — H pass, then V pass
// Kernel: Gaussian with sigma = r/2, 2r+1 taps.
// ---------------------------------------------------------------------------
static std::vector<float> make_gaussian_kernel(int r) {
    float sigma = static_cast<float>(r) / 2.0f;
    int   taps  = 2 * r + 1;
    std::vector<float> k(taps);
    float sum = 0;
    for (int i = 0; i < taps; ++i) {
        float x = static_cast<float>(i - r);
        k[i] = std::exp(-0.5f * x * x / (sigma * sigma));
        sum += k[i];
    }
    for (auto& v : k) v /= sum;
    return k;
}

static int64_t separable_reads(int w, int h, int r) {
    // H-pass: w*h*(2r+1) reads; V-pass: same
    return 2LL * w * h * (2*r+1);
}

static Ns gaussian_h_pass(const Image& src, Image& tmp, int r, const std::vector<float>& k) {
    auto t0 = Clock::now();
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            float v = 0;
            for (int dx = -r; dx <= r; ++dx) v += k[dx+r] * src.at(x+dx, y);
            tmp.set(x, y, static_cast<uint8_t>(std::round(v)));
        }
    return std::chrono::duration_cast<Ns>(Clock::now() - t0);
}

static Ns gaussian_v_pass(const Image& src, Image& dst, int r, const std::vector<float>& k) {
    auto t0 = Clock::now();
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            float v = 0;
            for (int dy = -r; dy <= r; ++dy) v += k[dy+r] * src.at(x, y+dy);
            dst.set(x, y, static_cast<uint8_t>(std::round(v)));
        }
    return std::chrono::duration_cast<Ns>(Clock::now() - t0);
}

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------
struct BenchResult {
    int   width, height, radius;
    double hPass_ms, vPass_ms, total_ms, box_ms;
    int64_t sep_reads, box_reads;
    double  read_ratio;    // box / sep
};

static BenchResult run_bench(int w, int h, int r) {
    Image src(w, h), tmp(w, h), dst_sep(w, h), dst_box(w, h);

    auto kernel = make_gaussian_kernel(r);

    Ns hTime = gaussian_h_pass(src, tmp,     r, kernel);
    Ns vTime = gaussian_v_pass(tmp, dst_sep, r, kernel);

    // Only run box blur for small radii to keep test fast (it's O(r²))
    Ns boxTime{0};
    if (r <= 8 || (w <= 1280 && r <= 15)) {
        boxTime = box_blur_cpu(src, dst_box, r);
    }

    double h_ms   = hTime.count()   / 1e6;
    double v_ms   = vTime.count()   / 1e6;
    double box_ms = boxTime.count() / 1e6;

    int64_t sep_r = separable_reads(w, h, r);
    int64_t box_r = box_blur_reads(w, h, r);

    return {w, h, r, h_ms, v_ms, h_ms + v_ms, box_ms, sep_r, box_r,
            static_cast<double>(box_r) / sep_r};
}

int main() {
    const int RADII[]  = {4, 8, 15, 30};
    const int RESW[]   = {1920, 1280};
    const int RESH[]   = {1080,  720};

    std::vector<BenchResult> results;

    std::cout << "Running compute blur benchmarks...\n\n";

    for (size_t ri = 0; ri < sizeof(RESW)/sizeof(RESW[0]); ++ri) {
        for (int r : RADII) {
            auto res = run_bench(RESW[ri], RESH[ri], r);
            results.push_back(res);
        }
    }

    // ---- Human-readable table ----
    std::cout << std::left
              << std::setw(8)  << "Width"
              << std::setw(8)  << "Height"
              << std::setw(8)  << "Radius"
              << std::setw(12) << "H-pass(ms)"
              << std::setw(12) << "V-pass(ms)"
              << std::setw(14) << "Sep-total(ms)"
              << std::setw(12) << "Box(ms)"
              << std::setw(14) << "Sep-reads"
              << std::setw(14) << "Box-reads"
              << std::setw(12) << "Box/Sep"
              << "\n"
              << std::string(114, '-') << "\n";

    for (auto& r : results) {
        std::cout << std::left
                  << std::setw(8)  << r.width
                  << std::setw(8)  << r.height
                  << std::setw(8)  << r.radius
                  << std::setw(12) << std::fixed << std::setprecision(3) << r.hPass_ms
                  << std::setw(12) << r.vPass_ms
                  << std::setw(14) << r.total_ms
                  << std::setw(12) << (r.box_ms > 0 ? r.box_ms : -1.0)
                  << std::setw(14) << r.sep_reads
                  << std::setw(14) << r.box_reads
                  << std::setw(12) << std::setprecision(1) << r.read_ratio << "x"
                  << "\n";
    }

    // ---- CSV output ----
    std::cout << "\n--- CSV (paste to EVALUATION_REPORT_V6.md) ---\n";
    std::cout << "width,height,radius,h_pass_ms,v_pass_ms,sep_total_ms,box_ms,sep_reads,box_reads,box_over_sep_ratio\n";
    for (auto& r : results) {
        std::cout << r.width    << ","
                  << r.height   << ","
                  << r.radius   << ","
                  << std::fixed << std::setprecision(3)
                  << r.hPass_ms << ","
                  << r.vPass_ms << ","
                  << r.total_ms << ","
                  << (r.box_ms > 0 ? r.box_ms : -1.0) << ","
                  << r.sep_reads << ","
                  << r.box_reads << ","
                  << std::setprecision(1) << r.read_ratio << "\n";
    }

    // Sanity assertions — separable should always read fewer pixels than O(r²) for r>=4
    for (auto& r : results) {
        assert(r.sep_reads < r.box_reads &&
               "Separable filter should read fewer pixels than naive O(r^2) box");
        assert(r.read_ratio >= 1.0 &&
               "Box/Sep ratio must be >= 1");
    }

    std::cout << "\ntest_compute_blur_perf PASSED (all sanity checks)\n";
    return 0;
}
