#ifndef REN_AAL_HPP
#define REN_AAL_HPP

#include <vector>
#include "vaslib/vas_math.hpp"

struct FColor;
struct TimeSpan;



/// Renderer for anti-aliased glowing lines
class RenAAL
{
public:
	bool draw_grid = false; ///< HACK
	
	static RenAAL& get(); ///< Returns singleton
	
	/// Draws line of specified solid color width and additional anti-aliased width
	virtual void draw_line(vec2fp p0, vec2fp p1, uint32_t clr, float width, float aa_width = 60.f, float clr_mul = 1.f) = 0;
	
	///
	virtual void draw_chain(const std::vector<vec2fp>& ps, bool loop, uint32_t clr, float width, float aa_width = 60.f, float clr_mul = 1.f) = 0;
	
	virtual void inst_begin(float grid_cell_size) = 0; ///< Starts building collection, discarding all
	virtual void inst_end() = 0; ///< Ends building collection
	virtual void inst_add(const std::vector<vec2fp>& ps, bool loop, float width = 0.1f, float aa_width = 3.f) = 0; ///< New object part (chain)
	virtual size_t inst_add_end() = 0; ///< Returns ID of finished object
	
	virtual void draw_inst(const Transform& tr, FColor clr, size_t id) = 0; ///< Draws instanced object
	
protected:
	friend class Postproc_Impl;
	static RenAAL* init();
	virtual ~RenAAL();
	
	virtual void render() = 0;
	virtual void render_grid(unsigned int fbo_out) = 0;
};

#endif // REN_AAL_HPP
