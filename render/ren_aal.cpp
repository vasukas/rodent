#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "ren_aal.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "shader.hpp"
#include "texture.hpp"



class RenAAL_Impl : public RenAAL
{
public:
	struct Obj
	{
		size_t off, count; // Vertices in buffer
		uint32_t clr;
		float clr_mul;
	};
	struct InstObj
	{
		Transform tr;
		size_t id;
		FColor clr;
	};
	
	GLA_VertexArray vao;
	std::vector<float> data_f; // Buffer data to send
	std::vector<uint8_t> data_i;
	
	size_t objs_off = 0; ///< Last vertex count
	std::vector<Obj> objs;
	
	Shader* sh;
	Shader* sh_inst;
	uint32_t prev_clr = 0;
	
	// for instanced drawing
	GLA_VertexArray inst_vao;
	std::vector<std::pair<size_t, size_t>> inst_objs;
	std::vector<InstObj> inst_q;
	
	GLA_Texture tex;
	
	
	
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
	
	size_t add_chain(const std::vector<vec2fp>& ps, bool loop, float width, float aa_width)
	{
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
		
		return n;
	}
	
	
	
	RenAAL_Impl()
	{
		vao.set_buffers({
			std::make_shared<GLA_Buffer>(4, GL_FLOAT, false, GL_STREAM_DRAW),
		    std::make_shared<GLA_Buffer>(3, GL_BYTE,  true,  GL_STREAM_DRAW)
		});
		sh = RenderControl::get().load_shader("aal");
		sh_inst = RenderControl::get().load_shader("aal_inst");
		
		const int n = 200;
		float data[n];
		
		float c = 0.2;
		float a = 1.f / (c * sqrt(2 * M_PI));
		float w = 2 * c * sqrt(2 * log(2));
		c = 2 * c * c;
		
		auto f = [&](float x){
			x = (1 - x) / w;
			return a * exp(-(x*x) / c);
		};
		float x1 = f(1);
		
		for (int i=0; i<n; ++i) {
			float x = (i + 1); x /= n;
			float y = f(x) / x1;
			data[i] = y;//std::min(255.f, 255 * y);
		}
		tex.bind();
		glTexImage2D(tex.target, 0, GL_R8, n, 1, 0, GL_RED, GL_FLOAT, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		
//		Texture::debug_save(tex.tex, "test.png", Texture::FMT_SINGLE);
//		exit(1);
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
		size_t n = add_chain(ps, loop, width, aa_width);
		add_objs(n-1, clr, clr_mul);
	}
	void render()
	{
		Camera* cam = RenderControl::get().get_world_camera();
		const float *mx = cam->get_full_matrix();
		const float scrmul = 1.f;//cam->get_state().mag;
		
		glActiveTexture(GL_TEXTURE0);
		tex.bind();
		
		if (!objs.empty())
		{
			vao.bufs[0]->update( data_f.size(), data_f.data() );
			vao.bufs[1]->update( data_i.size(), data_i.data() );
			vao.bind();
			
			sh->bind();
			sh->set4mx("proj", mx);
			sh->set1f("scrmul", scrmul);
			prev_clr = 0;
			
			for (auto& o : objs)
			{
				if (prev_clr != o.clr) {
					prev_clr = o.clr;
					sh->set_rgba("clr", o.clr, o.clr_mul);
				}
				glDrawArrays(GL_TRIANGLES, o.off, o.count);
			}
			
			data_f.clear();
			data_i.clear();
			objs_off = 0;
			objs.clear();
			prev_clr = 0;
		}
		if (!inst_q.empty())
		{
			inst_vao.bind();
			
			sh_inst->bind();
			sh_inst->set4mx("proj", mx);
			sh_inst->set1f("scrmul", scrmul);
			
			for (auto& o : inst_q)
			{
				auto cs = cossin_ft(o.tr.rot);
				sh_inst->set4f("pr", o.tr.pos.x, o.tr.pos.y, cs.x, cs.y);
				sh_inst->set_clr("clr", o.clr);
				
				auto& p = inst_objs[o.id];
				glDrawArrays(GL_TRIANGLES, p.first, p.second);
			}
			
			inst_q.clear();
		}
	}
	
	
	
	void inst_begin()
	{
		inst_vao.set_buffers({
			std::make_shared<GLA_Buffer>(4, GL_FLOAT, false, GL_STATIC_DRAW),
		    std::make_shared<GLA_Buffer>(3, GL_BYTE,  true,  GL_STATIC_DRAW)
		});
		inst_objs.clear();
	}
	void inst_end()
	{
		inst_vao.bufs[0]->update( data_f.size(), data_f.data() );
		inst_vao.bufs[1]->update( data_i.size(), data_i.data() );
		
		data_f.clear(); data_f.shrink_to_fit();
		data_i.clear(); data_i.shrink_to_fit();
	}
	void inst_add(const std::vector<vec2fp>& ps, bool loop, float width, float aa_width)
	{
		add_chain(ps, loop, width, aa_width);
	}
	size_t inst_add_end()
	{
		size_t last = inst_objs.empty() ? 0 : inst_objs.back().first + inst_objs.back().second;
		size_t cur = data_f.size() / 4;
		size_t id = inst_objs.size();
		inst_objs.emplace_back(last, cur - last);
		return id;
	}
	void draw_inst(const Transform& tr, FColor clr, size_t id)
	{
		reserve_more_block(inst_q, 512);
		inst_q.push_back({tr, id, clr});
	}
};



static RenAAL_Impl* rni;
RenAAL& RenAAL::get() {
	if (!rni) LOG_THROW_X("RenAAL::get() null");
	return *rni;
}
RenAAL* RenAAL::init() {return rni = new RenAAL_Impl;}
RenAAL::~RenAAL() {rni = nullptr;}
