cmake_minimum_required(VERSION 3.11)

project(rodent VERSION 0.1.0
               LANGUAGES CXX)

add_executable(rodent WIN32)
set(TARGET rodent)



option(USE_OPUSFILE "Compile with opus audio support" ON)
option(USE_SANITIZERS "Compile with all sanitizers (GCC/clang only)" OFF)

# pass 'PKG_OUTPUT_DIR' with path to copy all runtime files to after compilation
# pass 'USE_SDL_MAIN=1' to compile with SDL_main

if (USE_OPUSFILE)
	target_compile_definitions(${TARGET} PRIVATE USE_OPUSFILE=1)
endif()
if (USE_SDL_MAIN)
	target_compile_definitions(${TARGET} PRIVATE USE_SDL_MAIN=1)
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
	set(default_build_type "MinSizeRel")
	set(CMAKE_BUILD_TYPE ${default_build_type})
	message("No build type specified - set to ${default_build_type}")
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_SOURCE_DIR}/scripts")



target_compile_features(${TARGET} PUBLIC cxx_std_17)
target_compile_features(${TARGET} PUBLIC cxx_variadic_macros)
set_target_properties(${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Link-time optimization
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
	target_compile_options(${TARGET} PRIVATE /D_CRT_SECURE_NO_WARNINGS)  # unsafe C std functions
	target_compile_options(${TARGET} PRIVATE /wd4244 /wd4267 /wd4305)  # int<->float type conversions
	target_compile_options(${TARGET} PRIVATE /wd4005)  # repeated macro definitions
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
find_package(Threads  REQUIRED)

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

if (USE_OPUSFILE)
	find_package(opusfile QUIET)
	if (NOT ${opusfile_FOUND})
		add_subdirectory(scripts/opusfile)
	endif()
endif()



target_link_libraries(${TARGET} PRIVATE
	box2d
	fmt::fmt
	Freetype::Freetype
	GLEW::GLEW
	OpenGL::GL
	SDL2::SDL2
	Threads::Threads
)
list(APPEND DLL_NAMES
	freetype
	glew
	SDL2
)

if (USE_OPUSFILE)
	target_link_libraries(${TARGET} PRIVATE opusfile)
endif()

if (USE_SDL_MAIN)
	target_link_libraries(${TARGET} PRIVATE SDL2::SDL2main)
endif()

if (WIN32)
	target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()



# Source file list; no extension for cpp+hpp pairs
list(APPEND SRC_LIST

	client/ec_render
	client/effects
	client/level_map
	client/player_ui
	client/plr_input
	client/presenter
	client/replay
	client/resbase
	client/sounds

	core/hard_paths
	core/main.cpp
	core/main_loop
	core/settings
	core/vig

	game/common_defs.hpp
	game/damage
	game/entity
	game/game_core
	game/game_info_list
	game/game_mode
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
	game_objects/spawners2.cpp
	game_objects/tutorial
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

	utils/color_manip
	utils/ev_signal.hpp
	utils/image_utils
	utils/line_cfg
	utils/noise
	utils/path_search
	utils/res_audio
	utils/res_image
	utils/serializer_defs.hpp
	utils/serializer_dsl.hpp
	utils/serializer_guts.hpp
	utils/svg_simple
	utils/tcp_net
	utils/time_utils
	utils/tokenread

	vaslib/vas_atlas_packer
	vaslib/vas_containers
	vaslib/vas_cpp_utils
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

# Add sources to target and source groups
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

if (WIN32)
	set(RC_FILE "${PROJECT_SOURCE_DIR}/scripts/win32_icon.rc")
	target_sources(${TARGET} PRIVATE "${RC_FILE}")
	set_source_files_properties("${RC_FILE}" PROPERTIES LANGUAGE RC)
endif()



# Copy 'data/' and DLLs to packaging dir
if (PKG_OUTPUT_DIR)
	function(postbuild_cmd)
		add_custom_command(TARGET ${TARGET} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E ${ARGV})
	endfunction()
	
	get_filename_component(PKG_OUTPUT_DIR "${PKG_OUTPUT_DIR}" ABSOLUTE)
	postbuild_cmd(make_directory "${PKG_OUTPUT_DIR}")
	postbuild_cmd(copy $<TARGET_FILE:${TARGET}> "${PKG_OUTPUT_DIR}/$<TARGET_FILE_NAME:${TARGET}>")
	postbuild_cmd(copy_directory "${PROJECT_SOURCE_DIR}/data" "${PKG_OUTPUT_DIR}/data")
	postbuild_cmd(remove_directory "${PKG_OUTPUT_DIR}/user")
	
	if (WIN32)
		foreach(NAME ${DLL_NAMES})
			list(APPEND LIB_GLOBS "${CMAKE_PREFIX_PATH}/bin/*${NAME}*.dll")
			list(APPEND LIB_GLOBS "${CMAKE_PREFIX_PATH}/lib/*${NAME}*.dll")
		endforeach()
		file(GLOB LIBS ${LIB_GLOBS})
		postbuild_cmd(copy ${LIBS} "${PKG_OUTPUT_DIR}")
	endif()
endif()

