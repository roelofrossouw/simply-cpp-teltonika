cmake_minimum_required(VERSION 3.22)
set(SC_MODULE teltonika)
project(simply-cpp-${SC_MODULE} LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_TELTONIKA_BUILD_TESTS "Build simply-cpp-teltonika tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.5.21)
find_or_install_package(nlohmann_json nlohmann-json3-dev nlohmann-json)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

add_sc_object(BinaryParser)
add_sc_object(AVLType)
add_sc_object(Codec12)
add_sc_object(Teltonika
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

package_sc_module(
        DEPENDS "simply-cpp (>= 1.5.21~)"
        DEV_DEPENDS "simply-cpp-dev (>= 1.5.21~)" nlohmann-json3-dev)
