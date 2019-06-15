#ifndef REN_AAL_HPP
#define REN_AAL_HPP

#include <vector>
#include "vaslib/vas_math.hpp"

struct TimeSpan;



/// Renderer for anti-aliased glowing lines
class RenAAL
{
public:
	static RenAAL& get(); ///< Returns singleton
	
	/// Draws line of specified solid color width and additional anti-aliased width
	virtual void draw_line(vec2fp p0, vec2fp p1, uint32_t clr, float width, float aa_width = 60.f, float clr_mul = 1.f) = 0;
	
	///
	virtual void draw_chain(const std::vector<vec2fp>& ps, bool loop, uint32_t clr, float width, float aa_width = 60.f, float clr_mul = 1.f) = 0;
	
protected:
	friend class RenderControl_Impl;
	static RenAAL* init();
	virtual ~RenAAL();
	virtual void render() = 0;
};

#endif // REN_AAL_HPP
