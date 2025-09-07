#ifndef APPIMPLEMENTATION
#define APPIMPLEMENTATION 3
#endif


#if APPIMPLEMENTATION == 1
#include "SDF2D.hpp"
using AppImplementation = SDF2D;
#elif APPIMPLEMENTATION == 2
#include "SDF3D.hpp"
using AppImplementation = SDF3D;
#elif APPIMPLEMENTATION == 3
#include "SDFCornell.hpp"
using AppImplementation = SDFCornell;
#else
#error "Invalid APPIMPLEMENTATION value."
#endif

#include <stdexcept>
#include <iostream>

int main() {
    AppImplementation app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
