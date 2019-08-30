#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "vaslib/vas_math.hpp"



class Camera final
{
public:
	struct Frame
	{
		vec2fp pos = {}; ///< Center position
		float rot = 0.f; ///< Rotation (radians)
		float mag = 1.f; ///< Magnification factor
	};
	
	void set_state(Frame frm);
	const Frame& get_state() const {return cur;}
	
	void set_vport(Rect vp);
	void set_vport_full(); ///< Sets full window viewport
	const Rect& get_vport() const {return vport;}
	
	const float* get_full_matrix() const; ///< Returns 4x4 OpenGL projection & view matrix
	vec2fp mouse_cast(vec2i mou) const; ///< Returns world position from screen coords
	vec2i direct_cast(vec2fp p) const; ///< Returns screen coords from world position
	
private:
	Rect vport = {};
	Frame cur;
	
	mutable float mx_full[16];
	mutable float mx_rev[16];
	mutable int   mx_req_upd = 3;
};

#endif // CAMERA_HPP
