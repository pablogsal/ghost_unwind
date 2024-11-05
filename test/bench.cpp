#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <unistd.h>
#include <vector>

void read_file() {
  char buf[100];
  int fd = open("/etc/hostname", O_RDONLY);
  if (fd != -1) {
    read(fd, buf, sizeof(buf));
    close(fd);
  }
}

__attribute__((noinline, optimize("no-optimize-sibling-calls"))) int
recursive_function(int depth) {
  if (depth == 0) {
    std::cout << "Reached max depth, capturing stack trace..." << std::endl;

    // First unwinding
    auto start = std::chrono::steady_clock::now();
    read_file();
    auto end = std::chrono::steady_clock::now();
    auto first_unwind_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    std::cout << "First unwind duration: " << first_unwind_ns / 1000.0
              << " microseconds" << std::endl;

    // Measure N subsequent unwinds
    const int N = 10000; // Number of unwinding iterations
    std::vector<double> unwind_times;
    unwind_times.reserve(N);

    // Warm-up run
    for (int i = 0; i < 100; i++) {
      read_file();
    }

    // Actual timing
    for (int i = 0; i < N; i++) {
      start = std::chrono::steady_clock::now();
      read_file();
      end = std::chrono::steady_clock::now();
      double duration_us =
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
              .count() /
          1000.0;
      unwind_times.push_back(duration_us);
    }

    // Calculate statistics
    double sum = std::accumulate(unwind_times.begin(), unwind_times.end(), 0.0);
    double mean = sum / N;

    // Calculate min and max
    auto [min_it, max_it] =
        std::minmax_element(unwind_times.begin(), unwind_times.end());
    double min_time = *min_it;
    double max_time = *max_it;

    // Sort for median and percentiles
    std::sort(unwind_times.begin(), unwind_times.end());
    double median = N % 2 == 0
                        ? (unwind_times[N / 2 - 1] + unwind_times[N / 2]) / 2
                        : unwind_times[N / 2];

    std::cout << std::fixed << std::setprecision(3)
              << "\nUnwind Statistics (microseconds):\n"
              << "  Mean:   " << mean << "\n"
              << "  Median: " << median << "\n"
              << "  Min:    " << min_time << "\n"
              << "  Max:    " << max_time << "\n"
              << "  Samples:" << N << std::endl;

    return 42;
  }
  return recursive_function(depth - 1) + 1;
}

int main() {
  const int RECURSION_DEPTH = 1000;
  std::cout << "Starting recursive calls with depth " << RECURSION_DEPTH
            << std::endl;

  try {
    int res = recursive_function(RECURSION_DEPTH);
    std::cout << "Final result: " << res << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Exception caught: " << e.what() << std::endl;
  }

  return 0;
}
