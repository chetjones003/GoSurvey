#pragma once
// Public IO seam for point-cloud format count estimates and exporters.

#include <cstdint>
#include <string>
#include <vector>

namespace pointcloud_e57 {
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string& pathUtf8);
}

namespace pointcloud_pts {
struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string& pathUtf8);
[[nodiscard]] WriteResult WritePtsFile(const std::string& pathUtf8,
                                       const std::vector<double>& pointsXyz,
                                       const std::vector<float>& colorsRgb,
                                       const std::vector<float>& intensity);
}

namespace pointcloud_ptx {
struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string& pathUtf8);
[[nodiscard]] WriteResult WritePtxFile(const std::string& pathUtf8,
                                       const std::vector<double>& pointsXyz,
                                       const std::vector<float>& colorsRgb,
                                       const std::vector<float>& intensity);
}

namespace pointcloud_las {
struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string& pathUtf8);
[[nodiscard]] WriteResult WriteLasFile(const std::string& pathUtf8,
                                       const std::vector<double>& pointsXyz,
                                       const std::vector<float>& colorsRgb,
                                       const std::vector<float>& intensity);
}

namespace pointcloud_laz {
struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string& pathUtf8);
[[nodiscard]] WriteResult WriteLazFile(const std::string& pathUtf8,
                                       const std::vector<double>& pointsXyz,
                                       const std::vector<float>& colorsRgb,
                                       const std::vector<float>& intensity);
}
