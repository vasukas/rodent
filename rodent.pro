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

SOURCES += \
	client/presenter.cpp \
	client/resbase.cpp \
	core/gamepad.cpp \
	core/plr_control.cpp \
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
	game/damage.cpp \
	game/entity.cpp \
	game/game_core.cpp \
	game/level_ctr.cpp \
	game/movement.cpp \
	game/physics.cpp \
	game/player.cpp \
	game/weapon.cpp \
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
	render/shader.cpp \
	render/texture.cpp \
	settings.cpp \
	utils/block_cfg.cpp \
	utils/color_manip.cpp \
	utils/noise.cpp \
	utils/res_image.cpp \
	vaslib/vas_atlas_packer.cpp \
	vaslib/vas_file.cpp \
	vaslib/vas_font.cpp \
	vaslib/vas_log.cpp \
	vaslib/vas_math.cpp \
	vaslib/vas_misc.cpp \
	vaslib/vas_string_utils.cpp \
	vaslib/vas_time.cpp \
	vaslib/wincompat.cpp

HEADERS += \
	client/presenter.hpp \
	client/resbase.hpp \
	core/gamepad.hpp \
	core/plr_control.hpp \
	core/vig.hpp \
	external/Box2D/Box2D.h \
	external/Box2D/Collision/Shapes/b2ChainShape.h \
	external/Box2D/Collision/Shapes/b2CircleShape.h \
	external/Box2D/Collision/Shapes/b2EdgeShape.h \
	external/Box2D/Collision/Shapes/b2PolygonShape.h \
	external/Box2D/Collision/Shapes/b2Shape.h \
	external/Box2D/Collision/b2BroadPhase.h \
	external/Box2D/Collision/b2Collision.h \
	external/Box2D/Collision/b2Distance.h \
	external/Box2D/Collision/b2DynamicTree.h \
	external/Box2D/Collision/b2TimeOfImpact.h \
	external/Box2D/Common/b2BlockAllocator.h \
	external/Box2D/Common/b2Draw.h \
	external/Box2D/Common/b2GrowableStack.h \
	external/Box2D/Common/b2Math.h \
	external/Box2D/Common/b2Settings.h \
	external/Box2D/Common/b2StackAllocator.h \
	external/Box2D/Common/b2Timer.h \
	external/Box2D/Dynamics/Contacts/b2ChainAndCircleContact.h \
	external/Box2D/Dynamics/Contacts/b2ChainAndPolygonContact.h \
	external/Box2D/Dynamics/Contacts/b2CircleContact.h \
	external/Box2D/Dynamics/Contacts/b2Contact.h \
	external/Box2D/Dynamics/Contacts/b2ContactSolver.h \
	external/Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.h \
	external/Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.h \
	external/Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.h \
	external/Box2D/Dynamics/Contacts/b2PolygonContact.h \
	external/Box2D/Dynamics/Joints/b2DistanceJoint.h \
	external/Box2D/Dynamics/Joints/b2FrictionJoint.h \
	external/Box2D/Dynamics/Joints/b2GearJoint.h \
	external/Box2D/Dynamics/Joints/b2Joint.h \
	external/Box2D/Dynamics/Joints/b2MotorJoint.h \
	external/Box2D/Dynamics/Joints/b2MouseJoint.h \
	external/Box2D/Dynamics/Joints/b2PrismaticJoint.h \
	external/Box2D/Dynamics/Joints/b2PulleyJoint.h \
	external/Box2D/Dynamics/Joints/b2RevoluteJoint.h \
	external/Box2D/Dynamics/Joints/b2RopeJoint.h \
	external/Box2D/Dynamics/Joints/b2WeldJoint.h \
	external/Box2D/Dynamics/Joints/b2WheelJoint.h \
	external/Box2D/Dynamics/b2Body.h \
	external/Box2D/Dynamics/b2ContactManager.h \
	external/Box2D/Dynamics/b2Fixture.h \
	external/Box2D/Dynamics/b2Island.h \
	external/Box2D/Dynamics/b2TimeStep.h \
	external/Box2D/Dynamics/b2World.h \
	external/Box2D/Dynamics/b2WorldCallbacks.h \
	game/damage.hpp \
	game/entity.hpp \
	game/game_core.hpp \
	game/level_ctr.hpp \
	game/movement.hpp \
	game/physics.hpp \
	game/player.hpp \
	game/weapon.hpp \
	main_loop.hpp \
	render/camera.hpp \
	render/control.hpp \
	render/gl_utils.hpp \
	render/particles.hpp \
	render/postproc.hpp \
	render/ren_aal.hpp \
	render/ren_imm.hpp \
	render/ren_text.hpp \
	render/shader.hpp \
	render/texture.hpp \
	settings.hpp \
	utils/block_cfg.hpp \
	utils/color_manip.hpp \
	utils/ev_signal.hpp \
	utils/noise.hpp \
	utils/res_image.hpp \
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
