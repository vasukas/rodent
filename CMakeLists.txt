cmake_minimum_required(VERSION 3.9)

project(rodent VERSION 0.0.1
               LANGUAGES CXX)

add_executable(rodent)
set(TARGET rodent)

set(default_build_type "MinSizeRel")



option(USE_RELEASE_PATHS "Use system-defined paths for user data instead of current directory" OFF)

if (USE_RELEASE_PATHS)
	add_compile_definitions(USE_RELEASE_PATHS=1)
endif()

if (NOT DEFINED OpenGL_GL_PREFERENCE)
	set(OpenGL_GL_PREFERENCE "GLVND")
endif()

if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt AND NOT CMAKE_BUILD_TYPE)
	message("No build type specified - set to ${default_build_type}")
	set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "" FORCE)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)



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
		message("Built stripped")
	elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
		target_compile_definitions(${TARGET} PRIVATE _GLIBCXX_DEBUG=1 _GLIBCXX_DEBUG_PEDANTIC=1 _GLIBCXX_SANITIZE_VECTOR=1)
		target_compile_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		target_link_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		message("Built with sanitizers")
	endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(${TARGET} PRIVATE -Wno-unused-local-typedefs)
endif()



find_package(Freetype REQUIRED)
find_package(GLEW     REQUIRED)
find_package(OpenGL   REQUIRED)
find_package(SDL2     REQUIRED)

target_link_libraries(${TARGET} PRIVATE
	Freetype::Freetype
	GLEW::GLEW
	OpenGL::GL
	SDL2::SDL2
)



function(glob_src DIR)
	file(GLOB SRC "${PROJECT_SOURCE_DIR}/${DIR}/*.cpp" "${PROJECT_SOURCE_DIR}/${DIR}/*.cc")
	target_sources(${TARGET} PRIVATE ${SRC})
endfunction()

glob_src(external/Box2D/Collision)
glob_src(external/Box2D/Collision/Shapes)
glob_src(external/Box2D/Common)
glob_src(external/Box2D/Dynamics)
glob_src(external/Box2D/Dynamics/Contacts)
glob_src(external/Box2D/Dynamics/Joints)
glob_src(external/fmt)

target_include_directories(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/external")



list(APPEND SRC_LIST

	client/effects
	client/gamepad
	client/level_map
	client/plr_control
	client/presenter
	client/resbase

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
	game/player
	game/player_mgr
	game/s_objs
	game/weapon
	game/weapon_all

	game_ai/ai_common.hpp
	game_ai/ai_components
	game_ai/ai_drone
	game_ai/ai_group
	game_ai/ai_group_target

	render/camera
	render/control
	render/gl_utils
	render/particles
	render/postproc
	render/pp_graph
	render/ren_aal
	render/ren_imm
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
	utils/svg_simple
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
	if ("${NAME_EXT}" STREQUAL "")
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}.cpp")
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}.hpp")
	else()
		target_sources(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/${NAME}")
	endif()
endforeach()

