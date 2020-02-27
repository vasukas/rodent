cmake_minimum_required(VERSION 3.11)

project(rodent VERSION 0.0.1
               LANGUAGES CXX)

add_executable(rodent)
set(TARGET rodent)

set(default_build_type "MinSizeRel")



option(USE_RELEASE_PATHS "Use system-defined paths for user data instead of 'user' directory" OFF)
option(USE_SANITIZERS "Compile with all sanitizers" OFF)

if (USE_RELEASE_PATHS)
	target_compile_definitions(${TARGET} PRIVATE USE_RELEASE_PATHS=1)
endif()

if (NOT DEFINED OpenGL_GL_PREFERENCE)
	set(OpenGL_GL_PREFERENCE "GLVND" CACHE STRING "")
endif()

if (NOT CMAKE_BUILD_TYPE)
	message("No build type specified - set to ${default_build_type}")
	set(CMAKE_BUILD_TYPE ${default_build_type})
endif()



target_compile_features(${TARGET} PUBLIC cxx_std_17)
target_compile_features(${TARGET} PUBLIC cxx_variadic_macros)
set_target_properties(${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_res)
if (ipo_res AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	message("FLTO enabled")
else()
	message("FLTO not enabled")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR "${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
		target_link_options(${TARGET} PRIVATE -s)
		message("Build stripped")
	elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug" AND USE_SANITIZERS)
		target_compile_definitions(${TARGET} PRIVATE _GLIBCXX_DEBUG=1 _GLIBCXX_DEBUG_PEDANTIC=1 _GLIBCXX_SANITIZE_VECTOR=1)
		target_compile_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		target_link_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		message("Build with sanitizers")
	endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	target_compile_definitions(${TARGET} PRIVATE _ENABLE_ATOMIC_ALIGNMENT_FIX)
endif()

target_compile_definitions(${TARGET} PRIVATE _USE_MATH_DEFINES=1)
target_include_directories(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}")



include(FetchContent)

FetchContent_Declare(
	Box2D
	GIT_REPOSITORY  https://github.com/erincatto/Box2D.git
)
FetchContent_Declare(
	fmtlib
	GIT_REPOSITORY  https://github.com/fmtlib/fmt.git
)
FetchContent_Declare(
	stb
	GIT_REPOSITORY  https://github.com/nothings/stb.git
)

find_package(fmt      QUIET)
find_package(Freetype REQUIRED)
find_package(GLEW     REQUIRED)
find_package(OpenGL   REQUIRED)
find_package(SDL2     REQUIRED)

set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Box2D)
target_include_directories(${TARGET} PRIVATE "${box2d_SOURCE_DIR}/include")

FetchContent_Populate(stb)
target_include_directories(${TARGET} PRIVATE "${stb_SOURCE_DIR}")

if (NOT ${fmt_FOUND})
	FetchContent_MakeAvailable(fmtlib)
	target_include_directories(${TARGET} PRIVATE "${fmtlib_SOURCE_DIR}/include")
endif()



target_link_libraries(${TARGET} PRIVATE
	box2d
	fmt::fmt
	Freetype::Freetype
	GLEW::GLEW
	OpenGL::GL
)

if(TARGET SDL2::SDL2)
	target_link_libraries(${TARGET} PRIVATE SDL2::SDL2)
else()
	target_link_libraries(${TARGET} PRIVATE SDL2)
endif()

if (WIN32)
	target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()



list(APPEND SRC_LIST

	client/ec_render
	client/effects
	client/gamepad
	client/level_map
	client/player_ui
	client/plr_control
	client/presenter
	client/replay
	client/resbase

	core/hard_paths
	core/main.cpp
	core/main_loop
	core/settings
	core/vig

	game/common_defs.hpp
	game/damage
	game/entity
	game/game_core
	game/level_ctr
	game/level_gen
	game/physics
	game/player_mgr
	game/weapon

	game_ai/ai_algo
	game_ai/ai_common.hpp
	game_ai/ai_components
	game_ai/ai_control
	game_ai/ai_drone
	game_ai/ai_sim

	game_ctr/game_control
	game_ctr/game_ui

	game_objects/objs_basic
	game_objects/objs_creature
	game_objects/objs_player
	game_objects/spawners
	game_objects/weapon_all

	render/camera
	render/control
	render/gl_utils
	render/postproc
	render/pp_graph
	render/ren_aal
	render/ren_imm
	render/ren_particles
	render/ren_text
	render/shader
	render/texture

	utils/block_cfg
	utils/color_manip
	utils/ev_signal.hpp
	utils/image_utils
	utils/noise
	utils/path_search
	utils/res_image
	utils/serializer_defs.hpp
	utils/serializer_dsl.hpp
	utils/serializer_guts.hpp
	utils/svg_simple
	utils/tcp_net
	utils/time_utils

	vaslib/vas_atlas_packer
	vaslib/vas_containers
	vaslib/vas_cpp_utils.hpp
	vaslib/vas_file
	vaslib/vas_font
	vaslib/vas_log
	vaslib/vas_math
	vaslib/vas_misc
	vaslib/vas_string_utils
	vaslib/vas_time
	vaslib/vas_types.hpp
	vaslib/wincompat
)

foreach(NAME ${SRC_LIST})
	get_filename_component(NAME_EXT "${NAME}" EXT)
	get_filename_component(NAME_DIR "${NAME}" DIRECTORY)
	string(REPLACE "${PROJECT_SOURCE_DIR}" "" GROUP "${NAME_DIR}")

	if ("${NAME_EXT}" STREQUAL "")
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}.cpp")
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}.hpp")
		source_group("Headers/${GROUP}" FILES "${NAME}.hpp")
		source_group("Sources/${GROUP}" FILES "${NAME}.cpp")
	else()
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}")
		if    ("${NAME_EXT}" STREQUAL ".hpp")
			source_group("Headers/${GROUP}" FILES "${NAME}")
		elseif("${NAME_EXT}" STREQUAL ".cpp")
			source_group("Sources/${GROUP}" FILES "${NAME}")
		endif()
	endif()
endforeach()

