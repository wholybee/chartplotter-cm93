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

# ---- Packaging: Windows installer (CPack + NSIS) ----------------------------
#
# Wraps the plugin's two runtime files into a single NSIS installer .exe that
# drops them into an *existing* host install (the same layout the host's own
# installer produces), mirroring how the host application project packages
# itself:
#
#   $PROGRAMFILES64\HMV Chartplotter\plugins\chartplotter_cm93_plugin.dll
#   $PROGRAMFILES64\HMV Chartplotter\Qt6Concurrent.dll
#
# Only Qt6Concurrent.dll is bundled: the host is a Qt app and already ships
# Qt6Core/Gui/Widgets next to its exe, but not Concurrent (its own QtConcurrent
# use is header-only, so windeployqt never copied it) — that is the one Qt DLL
# the plugin adds to the host's import surface. $INSTDIR matches the host's
# CPACK_PACKAGE_INSTALL_DIRECTORY ("HMV Chartplotter"), so a default host install
# receives the files in place.
#
# Usage — build Release first, then run cpack from the build dir:
#
#   cmake --build --preset vs2022-release
#   cpack --config build/vs2022/CPackConfig.cmake -C Release -G NSIS
#
# Requires NSIS (makensis) on PATH:  https://nsis.sourceforge.io/Download
# `cmake --install build/vs2022 --config Release --prefix <dir>` stages the same
# two-file tree without NSIS, handy for verifying the payload.
if(WIN32)
    # The loadable module (a MODULE library) is a LIBRARY artifact on Windows;
    # name RUNTIME too so this is correct regardless of CMake's classification.
    install(TARGETS cm93_plugin
            LIBRARY DESTINATION "plugins"
            RUNTIME DESTINATION "plugins"
            CONFIGURATIONS Release)

    # The one Qt DLL the host lacks. install(FILES) accepts a generator
    # expression for the resolved DLL path.
    install(FILES "$<TARGET_FILE:Qt6::Concurrent>"
            DESTINATION "."
            CONFIGURATIONS Release)

    set(CPACK_GENERATOR "NSIS")
    # Keep the installer + CPack scratch dir inside the build tree.
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/installer")
    set(CPACK_PACKAGE_NAME            "HMV Chartplotter CM93 Plugin")
    set(CPACK_PACKAGE_VENDOR          "net.holybee")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CM93 v2 chart-source plugin for HMV Chartplotter")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    # Install into the host's folder so the plugin lands next to hmvchartplotter.exe.
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "HMV Chartplotter")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")

    # CRITICAL: the plugin shares the host's install *folder* but must NOT share
    # its Windows uninstall identity, or it will remove the host.
    #   * Registry key: defaults to CPACK_PACKAGE_INSTALL_DIRECTORY ("HMV
    #     Chartplotter") — identical to the host's key. With
    #     ENABLE_UNINSTALL_BEFORE_INSTALL that makes the plugin installer read the
    #     *host's* UninstallString and run it, uninstalling the host. Give the
    #     plugin its own key so uninstall-before-install only ever targets a prior
    #     plugin install (clean upgrade), and it gets its own Add/Remove entry.
    #   * Uninstaller name: CPack defaults to $INSTDIR\Uninstall.exe, which is the
    #     host's uninstaller in the shared folder — installing the plugin would
    #     overwrite it. Use a distinct name so both uninstallers coexist.
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "HMV Chartplotter CM93 Plugin")
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall-CM93-Plugin")

    # NSIS specifics: 64-bit Program Files, clean reinstall of the *plugin* only,
    # no PATH edits, no app shortcuts (this package ships no exe — it augments the
    # host).
    set(CPACK_NSIS_DISPLAY_NAME "HMV Chartplotter CM93 Plugin")
    set(CPACK_NSIS_PACKAGE_NAME "HMV Chartplotter CM93 Plugin")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)

    include(CPack)

    # ---- Auto-build the NSIS installer as part of a Release build ------------
    #
    # Folds `cpack --config .../CPackConfig.cmake -C Release -G NSIS` into the
    # normal build, exactly as the host project does. The `installer` target is
    # in ALL but only packages on a Release build (pack_release.cmake guards on
    # $<CONFIG>, since the install payload is Release-only) and depends on
    # cm93_plugin so the DLL exists first.
    #
    # Gated to MULTI-CONFIG generators (Visual Studio / MSVC). A single-config
    # build — notably the Ninja/MinGW CI job — sets no CMAKE_CONFIGURATION_TYPES,
    # so the target is NOT created there, keeping CI from invoking cpack/NSIS
    # (no makensis on that runner). Turn off with -DCM93_BUILD_INSTALLER=OFF.
    option(CM93_BUILD_INSTALLER
        "Auto-build the NSIS installer during a multi-config Release build" ON)
    if(CM93_BUILD_INSTALLER AND CMAKE_CONFIGURATION_TYPES)
        add_custom_target(installer ALL
            COMMAND ${CMAKE_COMMAND}
                    -DCFG=$<CONFIG>
                    -DCPACK_CMD=${CMAKE_CPACK_COMMAND}
                    -DCPACK_CONFIG=${CMAKE_BINARY_DIR}/CPackConfig.cmake
                    -DBIN_DIR=${CMAKE_BINARY_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/pack_release.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Packaging NSIS installer (Release only)"
            VERBATIM
        )
        add_dependencies(installer cm93_plugin)
    endif()
endif()
