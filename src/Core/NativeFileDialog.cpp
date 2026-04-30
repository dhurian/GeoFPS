#include "Core/NativeFileDialog.h"

#if !defined(__APPLE__)
namespace GeoFPS
{
std::string OpenNativeFileDialog(const std::string&, const std::vector<FileDialogFilter>&)
{
    return {};
}

std::string SaveNativeFileDialog(const std::string&, const std::vector<FileDialogFilter>&, const std::string&)
{
    return {};
}
} // namespace GeoFPS
#endif
