cmake_minimum_required(VERSION 3.21)

# ============================================================================
# CCW Animation Framework - SKSE Plugin for Skyrim SE
# ============================================================================
# Custom animation framework inspired by Carian Combo Warriors (Elden Ring)
# Provides: combo system, input buffering, animation event system,
#           Havok behavior graph hooks for dynamic animation replacement
# ============================================================================

project(
    CCWAnimFramework
    VERSION 1.0.0
    DESCRIPTION "Carian Combo Warriors Animation Framework for Skyrim SE"
    LANGUAGES CXX
)

# C++23 standard (CommonLibSSE-NG requirement)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CommonLibSSE-NG configuration
# Use 'auto' to support all SE/AE/VR versions via Address Library
set(COMMONLIBSSE_RUNTIME "auto" CACHE STRING "Target runtime")

# ============================================================================
# Dependencies
# ============================================================================

# CommonLibSSE-NG via vcpkg or add_subdirectory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG")
    add_subdirectory(extern/CommonLibSSE-NG)
else()
    find_package(CommonLibSSE CONFIG REQUIRED)
endif()

# nlohmann_json for config file parsing
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    # Fallback: use bundled single-header
    message(STATUS "nlohmann_json not found via vcpkg, will use bundled version if available")
endif()

# ============================================================================
# Plugin Source Files
# ============================================================================

set(PLUGIN_SOURCES
    src/main.cpp
    src/AnimationManager.cpp
    src/ComboSystem.cpp
    src/InputBuffer.cpp
    src/BehaviorHooks.cpp
    src/AnimEvents.cpp
)

set(PLUGIN_HEADERS
    src/AnimationManager.h
    src/ComboSystem.h
    src/InputBuffer.h
    src/BehaviorHooks.h
    src/AnimEvents.h
    src/CCWConfig.h
    src/PCH.h
)

# ============================================================================
# Build Target
# ============================================================================

add_library(${PROJECT_NAME} SHARED
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
)

if(nlohmann_json_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_NLOHMANN_JSON)
endif()

# Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# ============================================================================
# SKSE Plugin Metadata
# ============================================================================

# Plugin version info for SKSE
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_NAME="${PROJECT_NAME}"
    PLUGIN_VERSION="${PROJECT_VERSION}"
    PLUGIN_AUTHOR="CCW_AnimFramework_Dev"
)

# ============================================================================
# Output Configuration
# ============================================================================

# Output DLL name
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "CCWAnimFramework"
    SUFFIX ".dll"
)

# Copy to Skyrim SE Data/SKSE/Plugins after build (optional)
option(COPY_OUTPUT "Copy DLL to Skyrim SE SKSE/Plugins directory" OFF)
set(SKYRIM_DATA_DIR "" CACHE PATH "Path to Skyrim SE Data directory")

if(COPY_OUTPUT AND SKYRIM_DATA_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${SKYRIM_DATA_DIR}/SKSE/Plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${SKYRIM_DATA_DIR}/SKSE/Plugins/"
        COMMENT "Copying ${PROJECT_NAME}.dll to Skyrim SE SKSE/Plugins"
    )
endif()
