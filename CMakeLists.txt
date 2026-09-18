cmake_minimum_required(VERSION 3.20)

project(win-frame-filter VERSION 1.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Locate libobs.
#
# Two supported ways to point CMake at your OBS Studio development files:
#
#   1) You built OBS Studio from source (recommended, matches current OBS
#      plugin practice) and installed it, or at least configured it, so that
#      libobsConfig.cmake exists in its build/install tree. Point CMake at it:
#         -DCMAKE_PREFIX_PATH="C:/dev/obs-studio/build_x64/install"
#      find_package(libobs) below will pick it up.
#
#   2) You only have an OBS Studio *source* checkout (built or not) and want
#      to point directly at its include dir + obs.lib. Set:
#         -DOBS_SOURCE_DIR="C:/dev/obs-studio"
#         -DOBS_LIB="C:/dev/obs-studio/build_x64/libobs/RelWithDebInfo/obs.lib"
#      This repo falls back to that if find_package(libobs) fails.
#
# See README.md "Building on Windows" for the exact, full command sequence.
# ---------------------------------------------------------------------------

find_package(libobs QUIET)

add_library(win-frame-filter MODULE
	src/plugin-main.c
	src/win-frame-filter.c
	src/win-frame-render.c
	src/win-frame-styles.c
	src/win-frame-text.c
	src/win-frame-presets.c
	src/wf-prefs.c
	src/wf-alttab.c
	src/wf-notify.c
	src/wf-twitch.c
	src/wf-emotes.c
	src/wf-toast.c
	src/wf-assistant.c
	src/wf-update-screen.c
	src/wf-http.c
	src/wf-actions.c
	src/ui/menu-button.cpp
	src/ui/dialogs.cpp
)

# Where "Check for Updates" looks by default (a small JSON file: {"version","url"}).
set(WF_UPDATE_URL "https://raw.githubusercontent.com/ryxn3/obs-windows-plugin/main/update.json" CACHE STRING "Default update-check URL")
target_compile_definitions(win-frame-filter PRIVATE WF_PLUGIN_VERSION="${PROJECT_VERSION}" WF_DEFAULT_UPDATE_URL="${WF_UPDATE_URL}")

# OBS frontend API + Qt6 (for the menu-bar button). Both come from the same
# OBS build tree / deps bundle used to build libobs:
#   -DOBS_FRONTEND_LIB=C:/dev/obs-studio/build_x64/frontend/api/RelWithDebInfo/obs-frontend-api.lib
#   -DOBS_QT_DIR=C:/dev/obs-studio/.deps/obs-deps-qt6-<date>-x64
if(NOT OBS_FRONTEND_LIB OR NOT OBS_QT_DIR)
	message(FATAL_ERROR "Set -DOBS_FRONTEND_LIB=<obs-frontend-api.lib> and -DOBS_QT_DIR=<obs-deps-qt6 dir> (see README).")
endif()
list(APPEND CMAKE_PREFIX_PATH "${OBS_QT_DIR}")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_include_directories(win-frame-filter PRIVATE src)

if (TARGET OBS::libobs)
	target_link_libraries(win-frame-filter PRIVATE OBS::libobs)
elseif (libobs_FOUND)
	target_link_libraries(win-frame-filter PRIVATE libobs)
else()
	if (NOT OBS_SOURCE_DIR)
		message(FATAL_ERROR
			"libobs not found via find_package() and OBS_SOURCE_DIR is not set.\n"
			"Pass -DCMAKE_PREFIX_PATH=<obs install/build dir> or\n"
			"-DOBS_SOURCE_DIR=<obs-studio source checkout> -DOBS_LIB=<path to obs.lib>.\n"
			"See README.md for exact commands.")
	endif()
	if (NOT OBS_LIB)
		message(FATAL_ERROR "OBS_SOURCE_DIR was set but OBS_LIB (path to obs.lib) was not.")
	endif()
	target_include_directories(win-frame-filter PRIVATE
		"${OBS_SOURCE_DIR}/libobs"
	)
	# libobs.h pulls in the build-generated obsconfig.h; if you built OBS
	# with the standard CMakePresets.json flow, it lives under the preset's
	# binaryDir at config/obsconfig.h (e.g. build_x64/config/obsconfig.h).
	if(OBS_CONFIG_DIR)
		target_include_directories(win-frame-filter PRIVATE "${OBS_CONFIG_DIR}")
	endif()
	target_link_libraries(win-frame-filter PRIVATE "${OBS_LIB}")
endif()

target_include_directories(win-frame-filter PRIVATE "${OBS_SOURCE_DIR}/frontend/api")
target_link_libraries(win-frame-filter PRIVATE "${OBS_FRONTEND_LIB}" Qt6::Core Qt6::Widgets)

if (WIN32)
	target_link_libraries(win-frame-filter PRIVATE gdi32 user32 winhttp ws2_32 bcrypt ole32 windowscodecs shlwapi)
	target_compile_definitions(win-frame-filter PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

set_target_properties(win-frame-filter PROPERTIES
	PREFIX ""
	OUTPUT_NAME "win-frame-filter"
)

# ---------------------------------------------------------------------------
# data/ (effect shader + locale + built-in preset json) is deployed next to
# the built module, matching the standard OBS plugin layout:
#   <plugin>/bin/64bit/win-frame-filter.dll
#   <plugin>/data/...
# ---------------------------------------------------------------------------

add_custom_command(TARGET win-frame-filter POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/data"
		"${CMAKE_BINARY_DIR}/install-test/data/obs-plugins/win-frame-filter"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"$<TARGET_FILE_DIR:win-frame-filter>"
		"${CMAKE_BINARY_DIR}/install-test/obs-plugins/64bit"
	COMMENT "Assembling a ready-to-copy plugin layout under build/install-test/"
)

# ---------------------------------------------------------------------------
# install(): lays the plugin out the way a portable OBS install or the
# per-user OBS plugin directory expects it, so `cmake --install` alone is
# enough. See README.md "Installing into OBS Studio".
# ---------------------------------------------------------------------------

install(TARGETS win-frame-filter
	RUNTIME DESTINATION "obs-plugins/64bit"
	LIBRARY DESTINATION "obs-plugins/64bit"
)
install(DIRECTORY data/ DESTINATION "data/obs-plugins/win-frame-filter")

# ---------------------------------------------------------------------------
# Optional dev tool: offscreen render harness (see tools/render-test/).
#   cmake ... -DWF_BUILD_RENDER_TEST=ON
# ---------------------------------------------------------------------------
option(WF_BUILD_RENDER_TEST "Build the offscreen render harness" OFF)
if(WF_BUILD_RENDER_TEST AND OBS_LIB)
	add_executable(render-test tools/render-test/render_test.c src/win-frame-styles.c)
	target_include_directories(render-test PRIVATE src "${OBS_SOURCE_DIR}/libobs")
	if(OBS_CONFIG_DIR)
		target_include_directories(render-test PRIVATE "${OBS_CONFIG_DIR}")
	endif()
	target_link_libraries(render-test PRIVATE "${OBS_LIB}")
	target_compile_definitions(render-test PRIVATE _CRT_SECURE_NO_WARNINGS)

	add_executable(transition-test tools/render-test/transition_test.c)
	target_include_directories(transition-test PRIVATE "${OBS_SOURCE_DIR}/libobs")
	if(OBS_CONFIG_DIR)
		target_include_directories(transition-test PRIVATE "${OBS_CONFIG_DIR}")
	endif()
	target_link_libraries(transition-test PRIVATE "${OBS_LIB}")
	target_compile_definitions(transition-test PRIVATE _CRT_SECURE_NO_WARNINGS)

	add_executable(source-test tools/render-test/source_test.c)
	target_include_directories(source-test PRIVATE "${OBS_SOURCE_DIR}/libobs")
	if(OBS_CONFIG_DIR)
		target_include_directories(source-test PRIVATE "${OBS_CONFIG_DIR}")
	endif()
	target_link_libraries(source-test PRIVATE "${OBS_LIB}")
	target_compile_definitions(source-test PRIVATE _CRT_SECURE_NO_WARNINGS)

	add_executable(logic-test tools/render-test/logic_test.c src/wf-notify.c src/wf-twitch.c src/wf-emotes.c src/wf-actions.c src/wf-http.c src/wf-prefs.c
		src/win-frame-presets.c src/win-frame-styles.c)
	target_include_directories(logic-test PRIVATE src "${OBS_SOURCE_DIR}/libobs" "${OBS_SOURCE_DIR}/frontend/api")
	if(OBS_CONFIG_DIR)
		target_include_directories(logic-test PRIVATE "${OBS_CONFIG_DIR}")
	endif()
	target_link_libraries(logic-test PRIVATE "${OBS_LIB}" "${OBS_FRONTEND_LIB}" winhttp bcrypt ws2_32 ole32 windowscodecs shlwapi)
	target_compile_definitions(logic-test PRIVATE _CRT_SECURE_NO_WARNINGS WF_PLUGIN_VERSION="${PROJECT_VERSION}")
endif()
