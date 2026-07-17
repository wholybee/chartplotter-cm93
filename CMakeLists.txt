# CM93 chart-source plugin for HMV Chartplotter — standalone build.
#
# Builds a single dynamically-loaded Qt MODULE that the host application loads at
# runtime via QPluginLoader. It depends ONLY on Qt6 and the host's plugin-SDK
# headers (vendored in sdk/) — no GDAL, and no link against the host. See
# README.md and sdk/README.md.
#
# This plugin is GPL-2.0-or-later (its CM93 decoding derives from OpenCPN); the
# host application remains under its own LGPL-2.1 licence. See COPYING.
cmake_minimum_required(VERSION 3.21)
project(chartplotter_cm93_plugin VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# The host plugin-SDK headers (the ABI contract): chart_source.hpp,
# plugin_api.hpp, plugin_factory.hpp, chart_loader.hpp, projection.hpp. Vendored
# in sdk/; keep plugin_factory.hpp's kPluginAbiVersion + IID identical to the
# host you target. See sdk/README.md.
set(CHARTPLOTTER_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH
    "Directory containing the chartplotter plugin-SDK headers")

# ---- the plugin -------------------------------------------------------------
add_library(cm93_plugin MODULE
    src/cm93_plugin.hpp
    src/cm93_plugin.cpp
    src/cm93_plugin_factory.cpp
    src/cm93_chart_source.hpp
    src/cm93_chart_source.cpp
    src/cm93_dictionary.hpp
    src/cm93_dictionary.cpp
    src/cm93_decoder.hpp
    src/cm93_decoder.cpp
    src/cm93_plugin.json
)

set_target_properties(cm93_plugin PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "chartplotter_cm93_plugin"   # stable name in the host plugins/ dir
    PREFIX ""                                # no "lib" prefix on non-Windows
)

target_include_directories(cm93_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CHARTPLOTTER_SDK_DIR}"
)

# No host library is linked: the plugin uses the SDK headers only (pure-virtual
# interfaces + POD/inline types). Its import table is just Qt6Core + Qt6Concurrent.
target_link_libraries(cm93_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# ---- optional: install into an existing host build/install -------------------
# Set to the directory containing hmvchartplotter.exe. The plugin DLL is copied
# into its plugins/ subfolder, and Qt6Concurrent.dll next to the exe (the host
# does not deploy that DLL itself — its own QtConcurrent use is header-only, so
# windeployqt never copied it).
set(CHARTPLOTTER_APP_DIR "" CACHE PATH
    "Installed host app directory (the folder with hmvchartplotter.exe)")
if(CHARTPLOTTER_APP_DIR)
    add_custom_command(TARGET cm93_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CHARTPLOTTER_APP_DIR}/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:cm93_plugin>" "${CHARTPLOTTER_APP_DIR}/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Concurrent>" "${CHARTPLOTTER_APP_DIR}/"
        COMMENT "Installing CM93 plugin into ${CHARTPLOTTER_APP_DIR}"
        VERBATIM
    )
endif()

# ---- optional: standalone decoder smoke test --------------------------------
# A console tool that decodes a sample of cells from a real CM93 dataset (no GUI,
# no host). Run: test_cm93 <CM93root>   (needs Qt6Core.dll on PATH).
option(BUILD_CM93_TESTS "Build the CM93 decoder smoke test" ON)
if(BUILD_CM93_TESTS)
    add_executable(test_cm93
        test/test_cm93.cpp
        src/cm93_decoder.cpp
        src/cm93_dictionary.cpp
    )
    set_target_properties(test_cm93 PROPERTIES AUTOMOC OFF WIN32_EXECUTABLE FALSE)
    target_include_directories(test_cm93 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CHARTPLOTTER_SDK_DIR}")
    target_link_libraries(test_cm93 PRIVATE Qt6::Core)
endif()
