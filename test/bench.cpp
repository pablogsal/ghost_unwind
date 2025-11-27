/**
 * @file bench.cpp
 * @brief Performance benchmark: GhostStack vs libunwind baseline
 *
 * Measures:
 * 1. Initial capture time (uses libunwind internally)
 * 2. Subsequent capture time (GhostStack cached vs libunwind full)
 * 3. Throughput at various stack depths
 */

#include "ghost_stack.h"

#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <functional>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::nanoseconds;

//==============================================================================
// Benchmark Utilities
//==============================================================================

struct BenchmarkResult {
    const char* name;
    double mean_ns;
    double median_ns;
    double min_ns;
    double max_ns;
    double stddev_ns;
    size_t iterations;
    size_t frames_per_iter;
};

static double calculate_stddev(const std::vector<double>& times, double mean) {
    double sum_sq = 0;
    for (double t : times) {
        double diff = t - mean;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / times.size());
}

static BenchmarkResult run_benchmark(
    const char* name,
    size_t iterations,
    size_t warmup,
    std::function<size_t()> func)
{
    // Warmup
    size_t frames = 0;
    for (size_t i = 0; i < warmup; i++) {
        frames = func();
    }

    // Collect timings
    std::vector<double> times;
    times.reserve(iterations);

    for (size_t i = 0; i < iterations; i++) {
        auto start = Clock::now();
        func();
        auto end = Clock::now();
        times.push_back(static_cast<double>(
            std::chrono::duration_cast<Duration>(end - start).count()));
    }

    // Calculate statistics
    std::sort(times.begin(), times.end());

    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double mean = sum / static_cast<double>(times.size());
    double median = times[times.size() / 2];
    double min = times.front();
    double max = times.back();
    double stddev = calculate_stddev(times, mean);

    return {name, mean, median, min, max, stddev, iterations, frames};
}

static void print_result(const BenchmarkResult& r) {
    printf("%s:\n", r.name);
    printf("  Iterations:   %zu\n", r.iterations);
    printf("  Frames/iter:  %zu\n", r.frames_per_iter);
    printf("  Mean:         %.2f us\n", r.mean_ns / 1000.0);
    printf("  Median:       %.2f us\n", r.median_ns / 1000.0);
    printf("  Min:          %.2f us\n", r.min_ns / 1000.0);
    printf("  Max:          %.2f us\n", r.max_ns / 1000.0);
    printf("  Stddev:       %.2f us\n", r.stddev_ns / 1000.0);
}

//==============================================================================
// Baseline: Pure libunwind
//==============================================================================

__attribute__((noinline))
static size_t libunwind_unwind() {
    void* addresses[256];
    int ret = unw_backtrace(addresses, 256);
    return (ret > 0) ? static_cast<size_t>(ret) : 0;
}

//==============================================================================
// GhostStack: Initial capture (resets first)
//==============================================================================

__attribute__((noinline))
static size_t ghoststack_initial_capture() {
    ghost_stack_reset();
    void* frames[256];
    return ghost_stack_backtrace(frames, 256);
}

//==============================================================================
// GhostStack: Cached capture (uses shadow stack)
//==============================================================================

__attribute__((noinline))
static size_t ghoststack_cached_capture() {
    void* frames[256];
    return ghost_stack_backtrace(frames, 256);
}

//==============================================================================
// Recursive function for stack depth testing
//==============================================================================

__attribute__((noinline, optimize("no-optimize-sibling-calls")))
static size_t recurse_libunwind(int depth) {
    if (depth <= 0) {
        return libunwind_unwind();
    }
    return recurse_libunwind(depth - 1);
}

__attribute__((noinline, optimize("no-optimize-sibling-calls")))
static size_t recurse_ghost_initial(int depth) {
    if (depth <= 0) {
        return ghoststack_initial_capture();
    }
    return recurse_ghost_initial(depth - 1);
}

__attribute__((noinline, optimize("no-optimize-sibling-calls")))
static size_t recurse_ghost_cached(int depth) {
    if (depth <= 0) {
        return ghoststack_cached_capture();
    }
    return recurse_ghost_cached(depth - 1);
}

//==============================================================================
// Benchmark at specific depth
//==============================================================================

static void bench_at_depth(int depth, size_t iterations) {
    printf("\n=== Stack Depth: %d ===\n", depth);

    // Benchmark libunwind baseline
    auto libunwind_result = run_benchmark(
        "libunwind baseline",
        iterations, 10,
        [depth]() -> size_t {
            return recurse_libunwind(depth);
        }
    );
    print_result(libunwind_result);

    // Benchmark GhostStack initial capture
    auto ghost_initial = run_benchmark(
        "GhostStack initial",
        iterations, 10,
        [depth]() -> size_t {
            return recurse_ghost_initial(depth);
        }
    );
    print_result(ghost_initial);

    // Setup for cached benchmark - do initial capture once
    recurse_ghost_cached(depth);

    // Benchmark GhostStack cached capture
    auto ghost_cached = run_benchmark(
        "GhostStack cached",
        iterations, 10,
        [depth]() -> size_t {
            return recurse_ghost_cached(depth);
        }
    );
    print_result(ghost_cached);

    // Print speedup
    printf("\n");
    if (ghost_cached.mean_ns > 0) {
        printf("Speedup (cached vs libunwind): %.2fx\n",
               libunwind_result.mean_ns / ghost_cached.mean_ns);
    }

    // Cleanup
    ghost_stack_reset();
}

//==============================================================================
// Throughput benchmark
//==============================================================================

static void bench_throughput(size_t seconds) {
    printf("\n=== Throughput Benchmark (%zu seconds) ===\n", seconds);

    // libunwind throughput
    size_t libunwind_ops = 0;
    auto start = Clock::now();
    auto end_time = start + std::chrono::seconds(seconds);
    while (Clock::now() < end_time) {
        libunwind_unwind();
        libunwind_ops++;
    }
    auto elapsed = std::chrono::duration_cast<Duration>(Clock::now() - start).count();
    double libunwind_ops_per_sec = static_cast<double>(libunwind_ops) / (static_cast<double>(elapsed) / 1e9);

    printf("libunwind:  %.0f ops/sec\n", libunwind_ops_per_sec);

    // GhostStack cached throughput
    void* frames[256];
    ghost_stack_backtrace(frames, 256);  // Initial capture

    size_t ghost_ops = 0;
    start = Clock::now();
    end_time = start + std::chrono::seconds(seconds);
    while (Clock::now() < end_time) {
        ghost_stack_backtrace(frames, 256);
        ghost_ops++;
    }
    elapsed = std::chrono::duration_cast<Duration>(Clock::now() - start).count();
    double ghost_ops_per_sec = static_cast<double>(ghost_ops) / (static_cast<double>(elapsed) / 1e9);

    printf("GhostStack: %.0f ops/sec\n", ghost_ops_per_sec);
    printf("Speedup:    %.2fx\n", ghost_ops_per_sec / libunwind_ops_per_sec);

    ghost_stack_reset();
}

//==============================================================================
// Main
//==============================================================================

int main(int argc, char* argv[]) {
    printf("=== GhostStack Performance Benchmark ===\n\n");

    ghost_stack_init(nullptr);

    size_t iterations = 1000;
    if (argc > 1) {
        iterations = static_cast<size_t>(atoi(argv[1]));
    }

    printf("Iterations per benchmark: %zu\n", iterations);

    // Benchmark at different stack depths
    bench_at_depth(5, iterations);
    bench_at_depth(10, iterations);
    bench_at_depth(20, iterations);
    bench_at_depth(50, iterations);

    // Throughput benchmark
    bench_throughput(2);

    ghost_stack_thread_cleanup();
    return 0;
}
