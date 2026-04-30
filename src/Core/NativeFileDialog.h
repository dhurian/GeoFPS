#pragma once

#include <string>
#include <vector>

namespace GeoFPS
{
struct FileDialogFilter
{
    std::string name;
    std::vector<std::string> extensions;
};

[[nodiscard]] std::string OpenNativeFileDialog(const std::string& title,
                                               const std::vector<FileDialogFilter>& filters);
[[nodiscard]] std::string SaveNativeFileDialog(const std::string& title,
                                               const std::vector<FileDialogFilter>& filters,
                                               const std::string& defaultName);
} // namespace GeoFPS
