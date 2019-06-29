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
	Box2D/Collision/Shapes/b2ChainShape.cpp \
	Box2D/Collision/Shapes/b2CircleShape.cpp \
	Box2D/Collision/Shapes/b2EdgeShape.cpp \
	Box2D/Collision/Shapes/b2PolygonShape.cpp \
	Box2D/Collision/b2BroadPhase.cpp \
	Box2D/Collision/b2CollideCircle.cpp \
	Box2D/Collision/b2CollideEdge.cpp \
	Box2D/Collision/b2CollidePolygon.cpp \
	Box2D/Collision/b2Collision.cpp \
	Box2D/Collision/b2Distance.cpp \
	Box2D/Collision/b2DynamicTree.cpp \
	Box2D/Collision/b2TimeOfImpact.cpp \
	Box2D/Common/b2BlockAllocator.cpp \
	Box2D/Common/b2Draw.cpp \
	Box2D/Common/b2Math.cpp \
	Box2D/Common/b2Settings.cpp \
	Box2D/Common/b2StackAllocator.cpp \
	Box2D/Common/b2Timer.cpp \
	Box2D/Dynamics/Contacts/b2ChainAndCircleContact.cpp \
	Box2D/Dynamics/Contacts/b2ChainAndPolygonContact.cpp \
	Box2D/Dynamics/Contacts/b2CircleContact.cpp \
	Box2D/Dynamics/Contacts/b2Contact.cpp \
	Box2D/Dynamics/Contacts/b2ContactSolver.cpp \
	Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.cpp \
	Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.cpp \
	Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.cpp \
	Box2D/Dynamics/Contacts/b2PolygonContact.cpp \
	Box2D/Dynamics/Joints/b2DistanceJoint.cpp \
	Box2D/Dynamics/Joints/b2FrictionJoint.cpp \
	Box2D/Dynamics/Joints/b2GearJoint.cpp \
	Box2D/Dynamics/Joints/b2Joint.cpp \
	Box2D/Dynamics/Joints/b2MotorJoint.cpp \
	Box2D/Dynamics/Joints/b2MouseJoint.cpp \
	Box2D/Dynamics/Joints/b2PrismaticJoint.cpp \
	Box2D/Dynamics/Joints/b2PulleyJoint.cpp \
	Box2D/Dynamics/Joints/b2RevoluteJoint.cpp \
	Box2D/Dynamics/Joints/b2RopeJoint.cpp \
	Box2D/Dynamics/Joints/b2WeldJoint.cpp \
	Box2D/Dynamics/Joints/b2WheelJoint.cpp \
	Box2D/Dynamics/b2Body.cpp \
	Box2D/Dynamics/b2ContactManager.cpp \
	Box2D/Dynamics/b2Fixture.cpp \
	Box2D/Dynamics/b2Island.cpp \
	Box2D/Dynamics/b2World.cpp \
	Box2D/Dynamics/b2WorldCallbacks.cpp \
	console.cpp \
	core/dbg_menu.cpp \
	core/tui_layer.cpp \
	core/tui_surface.cpp \
	game/entity.cpp \
	game/game_core.cpp \
	game/movement.cpp \
	game/physics.cpp \
	game/presenter.cpp \
	game/presenter_res.cpp \
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
	utils/color_manip.cpp \
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
	Box2D/Box2D.h \
	Box2D/Collision/Shapes/b2ChainShape.h \
	Box2D/Collision/Shapes/b2CircleShape.h \
	Box2D/Collision/Shapes/b2EdgeShape.h \
	Box2D/Collision/Shapes/b2PolygonShape.h \
	Box2D/Collision/Shapes/b2Shape.h \
	Box2D/Collision/b2BroadPhase.h \
	Box2D/Collision/b2Collision.h \
	Box2D/Collision/b2Distance.h \
	Box2D/Collision/b2DynamicTree.h \
	Box2D/Collision/b2TimeOfImpact.h \
	Box2D/Common/b2BlockAllocator.h \
	Box2D/Common/b2Draw.h \
	Box2D/Common/b2GrowableStack.h \
	Box2D/Common/b2Math.h \
	Box2D/Common/b2Settings.h \
	Box2D/Common/b2StackAllocator.h \
	Box2D/Common/b2Timer.h \
	Box2D/Dynamics/Contacts/b2ChainAndCircleContact.h \
	Box2D/Dynamics/Contacts/b2ChainAndPolygonContact.h \
	Box2D/Dynamics/Contacts/b2CircleContact.h \
	Box2D/Dynamics/Contacts/b2Contact.h \
	Box2D/Dynamics/Contacts/b2ContactSolver.h \
	Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.h \
	Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.h \
	Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.h \
	Box2D/Dynamics/Contacts/b2PolygonContact.h \
	Box2D/Dynamics/Joints/b2DistanceJoint.h \
	Box2D/Dynamics/Joints/b2FrictionJoint.h \
	Box2D/Dynamics/Joints/b2GearJoint.h \
	Box2D/Dynamics/Joints/b2Joint.h \
	Box2D/Dynamics/Joints/b2MotorJoint.h \
	Box2D/Dynamics/Joints/b2MouseJoint.h \
	Box2D/Dynamics/Joints/b2PrismaticJoint.h \
	Box2D/Dynamics/Joints/b2PulleyJoint.h \
	Box2D/Dynamics/Joints/b2RevoluteJoint.h \
	Box2D/Dynamics/Joints/b2RopeJoint.h \
	Box2D/Dynamics/Joints/b2WeldJoint.h \
	Box2D/Dynamics/Joints/b2WheelJoint.h \
	Box2D/Dynamics/b2Body.h \
	Box2D/Dynamics/b2ContactManager.h \
	Box2D/Dynamics/b2Fixture.h \
	Box2D/Dynamics/b2Island.h \
	Box2D/Dynamics/b2TimeStep.h \
	Box2D/Dynamics/b2World.h \
	Box2D/Dynamics/b2WorldCallbacks.h \
	console.hpp \
	core/dbg_menu.hpp \
	core/tui_layer.hpp \
	core/tui_surface.hpp \
	game/entity.hpp \
	game/game_core.hpp \
	game/movement.hpp \
	game/physics.hpp \
	game/presenter.hpp \
	game/presenter_res.hpp \
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
	utils/color_manip.hpp \
	utils/ev_signal.hpp \
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
