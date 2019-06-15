#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "ren_aal.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "shader.hpp"



class RenAAL_Impl : public RenAAL
{
public:
	struct Obj
	{
		size_t off, count; // Vertices in buffer
		uint32_t clr;
		float clr_mui;
	};
	
	GLA_VertexArray vao;
	std::vector<float> data_f; // Buffer data to send
	std::vector<uint8_t> data_i;
	
	size_t objs_off = 0; ///< Last vertex count
	std::vector<Obj> objs;
	
	Shader* sh;
	uint32_t prev_clr = 0;
	
	
	
	void add_line(vec2fp p0, vec2fp p1, float width, float wpar, float aa_width)
	{
		vec2fp n;
		float len;
		
		if (!p0.equals(p1, 1e-5f))
		{
			n = p1 - p0;
			len = n.fastlen();
			
			vec2fp dir = n;
			dir *= 0.5 * aa_width / len;
			p0 -= dir;
			p1 += dir;
			
			n.rot90cw();
			n /= len;
		}
		else
		{
			n = {1, 0};
			len = 0.f;
			
			p0.x -= width / 2;
			p1.x += width / 2;
			n.rot90cw();
		}
		
		vec2fp u = n;
		n *= width * 0.5f;
		float x0 = p0.x + n.x, y0 = p0.y + n.y,
		      x1 = p1.x + n.x, y1 = p1.y + n.y,
		      x2 = p0.x - n.x, y2 = p0.y - n.y,
		      x3 = p1.x - n.x, y3 = p1.y - n.y;
		
		float endk = (len + aa_width) / aa_width;
		
#define PF(X) data_f.push_back(X)
		
#define PI(x) data_i.push_back( norm_i8(x) )
		
		// first triangle (11 - 21 - 12)
		
		PF( x0 ); PF( y0 ); PF(wpar); PF(endk);
		PI( u.x); PI( u.y); PI(-1);
		
		PF( x1 ); PF( y1 ); PF(wpar); PF(endk);
		PI( u.x); PI( u.y); PI( 1);
		
		PF( x2 ); PF( y2 ); PF(wpar); PF(endk);
		PI(-u.x); PI(-u.y); PI(-1);
		
		// second triangle (21 - 12 - 22)
		
		PF( x1 ); PF( y1 ); PF(wpar); PF(endk);
		PI( u.x); PI( u.y); PI( 1);
		
		PF( x2 ); PF( y2 ); PF(wpar); PF(endk);
		PI(-u.x); PI(-u.y); PI(-1);
		
		PF( x3 ); PF( y3 ); PF(wpar); PF(endk);
		PI(-u.x); PI(-u.y); PI( 1);
	}
	void add_objs(size_t n, uint32_t clr, float clr_mul)
	{
		n *= 6; // vertices per object
		if (prev_clr == clr) objs.back().count += n;
		else {
			prev_clr = clr;
			objs.push_back({ objs_off, n, clr, clr_mul });
		}
		objs_off += n;
	}
	
	
	
	RenAAL_Impl()
	{
		vao.set_buffers({
			std::make_shared<GLA_Buffer>(4, GL_FLOAT, false, GL_STREAM_DRAW),
		    std::make_shared<GLA_Buffer>(3, GL_BYTE,  true,  GL_STREAM_DRAW)
		});
		sh = RenderControl::get().load_shader("aal");
	}
	void draw_line(vec2fp p0, vec2fp p1, uint32_t clr, float width, float aa_width, float clr_mul)
	{
		if (!clr) return;
		if (aa_width < 1) aa_width = 1;
		width += aa_width;
		float wpar = width / aa_width;
		
		reserve_more_block(objs, 1024);
		reserve_more_block(data_f, 4096);
		reserve_more_block(data_i, 4096);
		
		add_line(p0, p1, width, wpar, aa_width);
		add_objs(1, clr, clr_mul);
	}
	void draw_chain(const std::vector<vec2fp>& ps, bool loop, uint32_t clr, float width, float aa_width, float clr_mul)
	{
		if (!clr || ps.size() < 2) return;
		if (aa_width < 1) aa_width = 1;
		width += aa_width;
		float wpar = width / aa_width;
		
		size_t dsz = std::max(static_cast<size_t>(4096), ps.size() * 6 * 4);
		reserve_more_block(objs, 1024);
		reserve_more_block(data_f, dsz);
		reserve_more_block(data_i, dsz);
		
		size_t n = ps.size();
		if (loop) ++n;
		for (size_t i = 1; i < n; ++i)
			add_line(ps[i%ps.size()], ps[i-1], width, wpar, aa_width);
		
		add_objs(n-1, clr, clr_mul);
	}
	void render()
	{
		vao.bufs[0]->update( data_f.size(), data_f.data() );
		vao.bufs[1]->update( data_i.size(), data_i.data() );
		vao.bind();
		
		const float *mx = RenderControl::get().get_world_camera()->get_full_matrix();
		
		sh->bind();
		sh->set4mx("proj", mx);
		prev_clr = 0;
		
		for (auto& o : objs)
		{
			if (prev_clr != o.clr) {
				prev_clr = o.clr;
				sh->set_rgba("clr", o.clr, o.clr_mui);
			}
			glDrawArrays(GL_TRIANGLES, o.off, o.count);
		}
		
		data_f.clear();
		data_i.clear();
		objs_off = 0;
		objs.clear();
		prev_clr = 0;
	}
};



static RenAAL_Impl* rni;
RenAAL& RenAAL::get() {
	if (!rni) LOG_THROW_X("RenAAL::get() null");
	return *rni;
}
RenAAL* RenAAL::init() {return rni = new RenAAL_Impl;}
RenAAL::~RenAAL() {rni = nullptr;}
