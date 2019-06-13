#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <queue>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"



class Camera final
{
public:
	struct Frame
	{
		vec2fp pos = {}; ///< Center position
		float rot = 0.f; ///< Rotation (radians)
		float mag = 1.f; ///< Magnification factor
		
		TimeSpan len; ///< How long it takes to get to this frame from previous
	};
	
	void set_state( Frame frm );
	void set_vport( Rect vp );
	void set_vport_full(); ///< Sets full window viewport
	
	void set_pos (vec2fp p); ///< Changes only current position
	
	const Rect&  get_vport() const { return vport; }
	const Frame& get_state() const { return cur;   }
	
	void add_frame      ( Frame frm ); ///< Adds new frame with absolute values
	void add_shift_frame( Frame frm ); ///< Adds new frame with position and rotation offseted from previous
	void clear(); ///< Clears all queued frames
	
	const float* get_full_matrix () const; ///< Returns 4x4 OpenGL projection & view matrix
	vec2fp mouse_cast( vec2i mou ) const; ///< Returns world position from screen coords
	
	void step( TimeSpan passed ); ///< Step animation
	
private:
	std::queue <Frame> ans;
	Frame fst;
	
	Rect vport;
	Frame cur;
	
	mutable float mx_full[16];
	mutable float mx_rev[16];
	mutable int   mx_req_upd = 3;
};

#endif // CAMERA_HPP
