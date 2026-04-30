#include "Core/NativeFileDialog.h"

#if defined(__APPLE__)
#import <AppKit/AppKit.h>

namespace
{
NSArray<NSString*>* AllowedFileTypes(const std::vector<GeoFPS::FileDialogFilter>& filters)
{
    NSMutableArray<NSString*>* fileTypes = [NSMutableArray array];
    for (const GeoFPS::FileDialogFilter& filter : filters)
    {
        for (const std::string& extension : filter.extensions)
        {
            std::string cleanExtension = extension;
            if (!cleanExtension.empty() && cleanExtension.front() == '.')
            {
                cleanExtension.erase(cleanExtension.begin());
            }
            if (!cleanExtension.empty())
            {
                [fileTypes addObject:[NSString stringWithUTF8String:cleanExtension.c_str()]];
            }
        }
    }
    return fileTypes.count > 0 ? fileTypes : nil;
}
} // namespace

namespace GeoFPS
{
std::string OpenNativeFileDialog(const std::string& title, const std::vector<FileDialogFilter>& filters)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = [NSString stringWithUTF8String:title.c_str()];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.allowedFileTypes = AllowedFileTypes(filters);

        if ([panel runModal] != NSModalResponseOK)
        {
            return {};
        }

        NSURL* url = panel.URL;
        if (url == nil || url.path == nil)
        {
            return {};
        }

        return std::string(url.path.UTF8String);
    }
}

std::string SaveNativeFileDialog(const std::string& title,
                                 const std::vector<FileDialogFilter>& filters,
                                 const std::string& defaultName)
{
    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.title = [NSString stringWithUTF8String:title.c_str()];
        panel.nameFieldStringValue = [NSString stringWithUTF8String:defaultName.c_str()];
        panel.allowedFileTypes = AllowedFileTypes(filters);
        panel.canCreateDirectories = YES;

        if ([panel runModal] != NSModalResponseOK)
        {
            return {};
        }

        NSURL* url = panel.URL;
        if (url == nil || url.path == nil)
        {
            return {};
        }

        return std::string(url.path.UTF8String);
    }
}
} // namespace GeoFPS
#endif
