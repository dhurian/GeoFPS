#include "Core/Application.h"
#include <exception>
#include <iostream>

int main()
{
    try
    {
        GeoFPS::Application app;
        if (!app.Initialize())
        {
            std::cerr << "Failed to initialize GeoFPS.\n";
            return 1;
        }

        app.Run();
        app.Shutdown();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << '\n';
        return 1;
    }
}
