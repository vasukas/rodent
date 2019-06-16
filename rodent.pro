TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

CONFIG(debug, debug|release) {
	CONFIG += gcc_sanitize
}
CONFIG(release, debug|release) {
	CONFIG += gcc_flto
}

SOURCES += \
	console.cpp \
	core/dbg_menu.cpp \
	core/tui_layer.cpp \
	core/tui_surface.cpp \
	main.cpp \
	main_loop.cpp \
	render/camera.cpp \
	render/control.cpp \
	render/gl_utils.cpp \
	render/particles.cpp \
	render/postproc.cpp \
	render/ren_aal.cpp \
	render/ren_imm.cpp \
	render/ren_text.cpp \
	render/ren_tui.cpp \
	render/shader.cpp \
	render/texture.cpp \
	settings.cpp \
	utils/block_cfg.cpp \
	utils/noise.cpp \
	utils/res_image.cpp \
	vaslib/vas_atlas_packer.cpp \
	vaslib/vas_file.cpp \
	vaslib/vas_font.cpp \
	vaslib/vas_log.cpp \
	vaslib/vas_math.cpp \
	vaslib/vas_string_utils.cpp \
	vaslib/vas_time.cpp \
	vaslib/wincompat.cpp

HEADERS += \
	console.hpp \
	core/dbg_menu.hpp \
	core/tui_layer.hpp \
	core/tui_surface.hpp \
	main_loop.hpp \
	render/camera.hpp \
	render/control.hpp \
	render/gl_utils.hpp \
	render/particles.hpp \
	render/postproc.hpp \
	render/ren_aal.hpp \
	render/ren_imm.hpp \
	render/ren_text.hpp \
	render/ren_tui.hpp \
	render/shader.hpp \
	render/texture.hpp \
	settings.hpp \
	utils/block_cfg.hpp \
	utils/noise.hpp \
	utils/res_image.hpp \
	vaslib/vas_atlas_packer.hpp \
	vaslib/vas_cpp_utils.hpp \
	vaslib/vas_file.hpp \
	vaslib/vas_font.hpp \
	vaslib/vas_log.hpp \
	vaslib/vas_math.hpp \
	vaslib/vas_string_utils.hpp \
	vaslib/vas_time.hpp \
	vaslib/vas_types.hpp \
	vaslib/wincompat.hpp

unix {
	CONFIG += link_pkgconfig
	PKGCONFIG += glew
	PKGCONFIG += fmt
	PKGCONFIG += freetype2
	PKGCONFIG += sdl2
}

linux-g++:gcc_sanitize {
	COMP_FLAGS = -fsanitize=address -fsanitize=undefined
	QMAKE_CXXFLAGS += $${COMP_FLAGS}
	QMAKE_LFLAGS += $${COMP_FLAGS}
}

linux-g++:gcc_flto {
	COMP_FLAGS = -flto
	QMAKE_CXXFLAGS += $${COMP_FLAGS}
	QMAKE_LFLAGS += $${COMP_FLAGS}
}
