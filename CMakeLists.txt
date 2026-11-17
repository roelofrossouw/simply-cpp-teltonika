cmake_minimum_required(VERSION 3.22)
set(SC_MODULE teltonika)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-teltonika" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_TELTONIKA_BUILD_TESTS "Build simply-cpp-teltonika tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 3.0.0) # log_level.h
find_or_install_package(nlohmann_json nlohmann-json3-dev nlohmann-json)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

# command.h and teltonika.h are the public headers; the parser, the AVL table and byte order are
# private (src/), so they build as part of the objects that use them.
add_sc_object(command SOURCES src/byte_order.h)
add_sc_object(teltonika
        SOURCES src/binary_parser.cpp src/binary_parser.h src/avl_type.cpp src/avl_type.h src/byte_order.h
        LINK_LIBRARIES sc::sc-core nlohmann_json::nlohmann_json
        PUBLIC_LINK_LIBRARIES sc::sc-core nlohmann_json::nlohmann_json)

add_sc_libraries(DESCRIPTION "simply-cpp-teltonika")

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-teltonika
        COMPONENT development)

if (SC_TELTONIKA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-teltonika-test)
endif ()

# sc-core is built into the library, so the runtime package doesn't need simply-cpp.
package_sc_module(
        DEV_DEPENDS "simply-cpp-dev (>= 3.0.0~)" nlohmann-json3-dev)
