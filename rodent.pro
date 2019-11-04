TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += $$PWD/external

CONFIG(debug, debug|release) {
	CONFIG += gcc_sanitize
}
CONFIG(release, debug|release) {
	CONFIG += gcc_flto
}

unix {
	CONFIG += link_pkgconfig
	PKGCONFIG += glew
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

QMAKE_CXXFLAGS += -Wno-unused-local-typedefs

SOURCES += \
	client/gamepad.cpp \
	client/level_map.cpp \
	client/plr_control.cpp \
	client/presenter.cpp \
	client/resbase.cpp \
	core/main.cpp \
	core/main_loop.cpp \
	core/settings.cpp \
	core/vig.cpp \
	external/Box2D/Collision/Shapes/b2ChainShape.cpp \
	external/Box2D/Collision/Shapes/b2CircleShape.cpp \
	external/Box2D/Collision/Shapes/b2EdgeShape.cpp \
	external/Box2D/Collision/Shapes/b2PolygonShape.cpp \
	external/Box2D/Collision/b2BroadPhase.cpp \
	external/Box2D/Collision/b2CollideCircle.cpp \
	external/Box2D/Collision/b2CollideEdge.cpp \
	external/Box2D/Collision/b2CollidePolygon.cpp \
	external/Box2D/Collision/b2Collision.cpp \
	external/Box2D/Collision/b2Distance.cpp \
	external/Box2D/Collision/b2DynamicTree.cpp \
	external/Box2D/Collision/b2TimeOfImpact.cpp \
	external/Box2D/Common/b2BlockAllocator.cpp \
	external/Box2D/Common/b2Draw.cpp \
	external/Box2D/Common/b2Math.cpp \
	external/Box2D/Common/b2Settings.cpp \
	external/Box2D/Common/b2StackAllocator.cpp \
	external/Box2D/Common/b2Timer.cpp \
	external/Box2D/Dynamics/Contacts/b2ChainAndCircleContact.cpp \
	external/Box2D/Dynamics/Contacts/b2ChainAndPolygonContact.cpp \
	external/Box2D/Dynamics/Contacts/b2CircleContact.cpp \
	external/Box2D/Dynamics/Contacts/b2Contact.cpp \
	external/Box2D/Dynamics/Contacts/b2ContactSolver.cpp \
	external/Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.cpp \
	external/Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.cpp \
	external/Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.cpp \
	external/Box2D/Dynamics/Contacts/b2PolygonContact.cpp \
	external/Box2D/Dynamics/Joints/b2DistanceJoint.cpp \
	external/Box2D/Dynamics/Joints/b2FrictionJoint.cpp \
	external/Box2D/Dynamics/Joints/b2GearJoint.cpp \
	external/Box2D/Dynamics/Joints/b2Joint.cpp \
	external/Box2D/Dynamics/Joints/b2MotorJoint.cpp \
	external/Box2D/Dynamics/Joints/b2MouseJoint.cpp \
	external/Box2D/Dynamics/Joints/b2PrismaticJoint.cpp \
	external/Box2D/Dynamics/Joints/b2PulleyJoint.cpp \
	external/Box2D/Dynamics/Joints/b2RevoluteJoint.cpp \
	external/Box2D/Dynamics/Joints/b2RopeJoint.cpp \
	external/Box2D/Dynamics/Joints/b2WeldJoint.cpp \
	external/Box2D/Dynamics/Joints/b2WheelJoint.cpp \
	external/Box2D/Dynamics/b2Body.cpp \
	external/Box2D/Dynamics/b2ContactManager.cpp \
	external/Box2D/Dynamics/b2Fixture.cpp \
	external/Box2D/Dynamics/b2Island.cpp \
	external/Box2D/Dynamics/b2World.cpp \
	external/Box2D/Dynamics/b2WorldCallbacks.cpp \
	external/fmt/format.cc \
	external/fmt/posix.cc \
	game/damage.cpp \
	game/entity.cpp \
	game/game_core.cpp \
	game/level_gen.cpp \
	game/level_ctr.cpp \
	game/physics.cpp \
	game/player.cpp \
	game/player_mgr.cpp \
	game/s_objs.cpp \
	game/weapon.cpp \
	game/weapon_all.cpp \
	game_ai/ai_components.cpp \
	game_ai/ai_drone.cpp \
	game_ai/ai_group.cpp \
	game_ai/ai_group_target.cpp \
	render/camera.cpp \
	render/control.cpp \
	render/gl_utils.cpp \
	render/particles.cpp \
	render/postproc.cpp \
	render/pp_graph.cpp \
	render/ren_aal.cpp \
	render/ren_imm.cpp \
	render/ren_text.cpp \
	render/shader.cpp \
	render/texture.cpp \
	utils/block_cfg.cpp \
	utils/color_manip.cpp \
	utils/image_utils.cpp \
	utils/noise.cpp \
	utils/path_search.cpp \
	utils/res_image.cpp \
	utils/svg_simple.cpp \
	utils/time_utils.cpp \
	vaslib/vas_atlas_packer.cpp \
	vaslib/vas_containers.cpp \
	vaslib/vas_file.cpp \
	vaslib/vas_font.cpp \
	vaslib/vas_log.cpp \
	vaslib/vas_math.cpp \
	vaslib/vas_misc.cpp \
	vaslib/vas_string_utils.cpp \
	vaslib/vas_time.cpp \
	vaslib/wincompat.cpp

HEADERS += \
	client/gamepad.hpp \
	client/level_map.hpp \
	client/plr_control.hpp \
	client/presenter.hpp \
	client/resbase.hpp \
	core/main_loop.hpp \
	core/settings.hpp \
	core/vig.hpp \
	game/common_defs.hpp \
	game/damage.hpp \
	game/entity.hpp \
	game/game_core.hpp \
	game/level_gen.hpp \
	game/level_ctr.hpp \
	game/physics.hpp \
	game/player.hpp \
	game/player_mgr.hpp \
	game/s_objs.hpp \
	game/weapon.hpp \
	game/weapon_all.hpp \
	game_ai/ai_common.hpp \
	game_ai/ai_components.hpp \
	game_ai/ai_drone.hpp \
	game_ai/ai_group.hpp \
	game_ai/ai_group_target.hpp \
	render/camera.hpp \
	render/control.hpp \
	render/gl_utils.hpp \
	render/particles.hpp \
	render/postproc.hpp \
	render/pp_graph.hpp \
	render/ren_aal.hpp \
	render/ren_imm.hpp \
	render/ren_text.hpp \
	render/shader.hpp \
	render/texture.hpp \
	utils/block_cfg.hpp \
	utils/color_manip.hpp \
	utils/ev_signal.hpp \
	utils/image_utils.hpp \
	utils/noise.hpp \
	utils/path_search.hpp \
	utils/res_image.hpp \
	utils/svg_simple.hpp \
	utils/time_utils.hpp \
	vaslib/vas_atlas_packer.hpp \
	vaslib/vas_containers.hpp \
	vaslib/vas_cpp_utils.hpp \
	vaslib/vas_file.hpp \
	vaslib/vas_font.hpp \
	vaslib/vas_log.hpp \
	vaslib/vas_math.hpp \
	vaslib/vas_misc.hpp \
	vaslib/vas_string_utils.hpp \
	vaslib/vas_time.hpp \
	vaslib/vas_types.hpp \
	vaslib/wincompat.hpp
