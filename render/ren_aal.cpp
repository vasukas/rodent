#include <future>
#include "core/settings.hpp"
#include "client/resbase.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "ren_aal.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "shader.hpp"
#include "texture.hpp"



struct Noise
{
	Noise()
	{
		tex.target = GL_TEXTURE_3D;
	}
	void generate(float cell_size)
	{
		size = 32;
		depth = 20;
		ck = cell_size;
		
		has_tex = false;
		tex_async.reset();
		
		tex_async = std::async(std::launch::async, [this]
		{
			set_this_thread_name("aal grid gen");
			
			std::vector<uint8_t> img_px( size * size * depth * 3 );
			TimeSpan time0 = TimeSpan::current();
			
			std::vector<float> prev_vals( size * size * 2 );
			RandomGen rnd;
			
			for (int z = 0; z < depth; ++z)
			for (int y = 0; y < size;  ++y)
			for (int x = 0; x < size;  ++x)
			{
				float nH = rnd.range_n();
				float nV = rnd.range_n();
				
				float& pH = prev_vals[(y * size + x)*2 + 0];
				float& pV = prev_vals[(y * size + x)*2 + 1];
				float H, V;
				
				if (!z) {
					H = pH = nH;
					V = pV = nV;
				}
				else {
					auto calc = [](float& old, float cur)
					{
						float v = cur - old;
						float len = std::fabs(v);
						if (len > 0.3) v *= 0.3 / len;
						
						v += old;
						old = cur;
						return v;
					};
					H = calc(pH, nH);
					V = calc(pV, nV);
				}
				
				H = lerp<float>(170, 210, H) / 360;
				V = lerp(0.15, 0.30, V);
				
				uint8_t* px = img_px.data() + (z * (size * size) + y * size + x)*3;
				uint32_t px_val = FColor(H, 1, V).hsv_to_rgb().to_px();
				px[0] = px_val >> 24;
				px[1] = px_val >> 16;
				px[2] = px_val >> 8;
			}
			
			VLOGI("RenAAL generation time: {:.3f} seconds", (TimeSpan::current() - time0).seconds());
			return img_px;
		});
	}
	bool check()
	{
		if (tex_async && tex_async->wait_for (std::chrono::seconds(0)) == std::future_status::ready)
		{
			auto res = tex_async->get();
			tex_async.reset();
			has_tex = true;
			
			tex.bind();
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, size, size, depth, 0, GL_RGB, GL_UNSIGNED_BYTE, res.data());
			
			tex.set_byte_size( size * size * depth * 3 );
		}
		return has_tex;
	}
	void setup(Shader& sh, TimeSpan passed)
	{
		glActiveTexture(GL_TEXTURE1);
		tex.bind();
		
		sh.set1f("t", t / depth * 0.7);
		t += passed.seconds();
		
		auto& cam = RenderControl::get().get_world_camera();
		auto  ssz = RenderControl::get().get_size();
		
		vec2fp ssz_k = {float(ssz.x) / ssz.y, 1};
		ssz_k /= ck;
		
		vec2fp scr_z = cam.coord_size();
		vec2fp cpos = cam.get_state().pos;
		cpos.y = -cpos.y;
		cpos *= 0.8; // looks bit better for some reason
		
		sh.set2f("offset", cpos / (scr_z / ssz_k));
		sh.set2f("scrk", ssz_k.x, ssz_k.y);
	}
	
private:
	int size = 1; // x,y
	int depth = 1; // z (time)
	float ck = 1; // cell size
	
	GLA_Texture tex;
	float t = 0.f;
	
	bool has_tex = false;
	std::optional <std::future< std::vector<uint8_t> >> tex_async;
};



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

	size_t objs_off = 0; ///< Last vertex count
	std::vector<Obj> objs;
	
	std::unique_ptr<Shader> sh, sh_inst;
	uint32_t prev_clr = 0;
	
	GLA_Texture tex;
	
	// for instanced drawing
	GLA_VertexArray inst_vao;
	std::vector<std::pair<size_t, size_t>> inst_objs;
	std::vector<InstObj> inst_q;
	bool inst_locked = false; // prevent render while building in process
	
	// for grid
	GLA_Framebuffer fbo;
	GLA_Texture fbo_clr;
	std::unique_ptr<Shader> fbo_sh;
	RAII_Guard fbo_g;
	Noise fbo_noi;
	
	//
	RAII_Guard sett_g;
	
	
	
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
#define PI PF
		
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
		
		size_t dsz = ps.size() * 6 * 7;
		if (4096 > dsz) dsz = 4096;
		reserve_more_block(objs, 1024);
		reserve_more_block(data_f, dsz);

		size_t n = ps.size();
		if (loop) ++n;
		for (size_t i = 1; i < n; ++i)
			add_line(ps[i%ps.size()], ps[i-1], width, wpar, aa_width);
		
		return n;
	}
	
	
	
	RenAAL_Impl()
	{
		auto buf = std::make_shared<GLA_Buffer>(0);
		vao.set_attribs({ {buf, 4}, {buf, 3} });
		
		sh      = Shader::load("aal", {}, true);
		sh_inst = Shader::load("aal_inst", {}, true);
		
		sett_g = AppSettings::get_mut().add_cb([this]{
			reinit_glow();
		});
		
		// for grid
		
		fbo_sh = Shader::load("pp/aal_grid", {[](Shader& sh){ sh.set1i("noi", 1); }});
		fbo_g = RenderControl::get().add_size_cb([this]{ fbo_clr.set(GL_RGBA, RenderControl::get_size(), 0, 4); }, true);

		fbo.bind();
		fbo.attach_tex(GL_COLOR_ATTACHMENT0, fbo_clr);
	}
	void reinit_glow()
	{
		const int n = 200;
		float data[n];
		
		auto init = [&](auto f) {
			float x1 = f(1);
			for (int i=0; i<n; ++i) {
				float x = (i + 1); x /= n;
				float y = f(x) / x1;
				data[i] = y;
			}
		};
		auto init_old = [&](float c){
			float a = 1.f / (c * sqrt(2 * M_PI));
			float w = 2 * c * sqrt(2 * log(2));
			c = 2 * c * c;
			init([&](float x){
				x = (1 - x) / w;
				return a * exp(-(x*x) / c);
			});
		};
		switch (AppSettings::get().aal_type)
		{
		case AppSettings::AAL_OldFuzzy:
			init_old(0.2);
			break;
		
		case AppSettings::AAL_CrispGlow:
			init([](float x){ return x*x; });
			break;
			
		case AppSettings::AAL_Clear:
			init_old(0.17);
			break;
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
		if (inst_locked) return;
		
		const float *mx = RenderControl::get().get_world_camera().get_full_matrix();
		const float scrmul = 1.f;//cam->get_state().mag;
		
		glActiveTexture(GL_TEXTURE0);
		tex.bind();
		
		if (!objs.empty())
		{
			vao.bufs[0]->update( data_f.size(), data_f.data() );
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
			objs_off = 0;
			objs.clear();
			prev_clr = 0;
		}
		if (!inst_q.empty() || draw_grid)
		{
			inst_vao.bind();
			
			sh_inst->bind();
			sh_inst->set4mx("proj", mx);
			sh_inst->set1f("scrmul", scrmul);
			
			for (auto& o : inst_q)
			{
				auto cs = cossin_lut(o.tr.rot);
				sh_inst->set4f("obj_tr", o.tr.pos.x, o.tr.pos.y, cs.x, cs.y);
				sh_inst->set_clr("clr", o.clr);
				
				auto& p = inst_objs[o.id];
				glDrawArrays(GL_TRIANGLES, p.first, p.second);
			}
			
			inst_q.clear();
		}
	}
	void render_grid(unsigned int fbo_out)
	{
		if (!draw_grid) return;
		if (!fbo_noi.check()) return;
		
		// draw to buffer
		
		fbo.bind();
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
		glBlendEquation(GL_MAX);
		
		inst_vao.bind();
		
		const float *mx = RenderControl::get().get_world_camera().get_full_matrix();
		const float scrmul = 1.f;//cam->get_state().mag;
		
		glActiveTexture(GL_TEXTURE0);
		tex.bind();
		
		sh_inst->bind();
		sh_inst->set4mx("proj", mx);
		sh_inst->set1f("scrmul", scrmul);
		
//		FColor clr(0, 0.8, 1, 0.3);
//		clr *= clr.a;
		const FColor clr(1, 1, 1, 1);
		
		sh_inst->set4f("obj_tr", 0, 0, 1, 0);
		sh_inst->set_clr("clr", clr);
		
		auto& p = inst_objs[MODEL_LEVEL_GRID];
		glDrawArrays(GL_TRIANGLES, p.first, p.second);
		
		// draw to screen
		
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_out);
		
		glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE, GL_ONE);
		glBlendEquation(GL_FUNC_ADD);
		
		fbo_sh->bind();
		fbo_noi.setup(*fbo_sh, RenderControl::get().get_passed());
		
		glActiveTexture(GL_TEXTURE0);
		fbo_clr.bind();
		
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	
	
	
	void inst_begin(float grid_cell_size)
	{
		auto buf = std::make_shared<GLA_Buffer>(0);
		inst_vao.set_attribs({ {buf, 4}, {buf, 3} });
		inst_objs.clear();
		
		fbo_noi.generate(grid_cell_size);
		inst_locked = true;
	}
	void inst_end()
	{
		inst_vao.bufs[0]->update( data_f.size(), data_f.data() );
		data_f.clear(); data_f.shrink_to_fit();
		inst_locked = false;
	}
	void inst_add(const std::vector<vec2fp>& ps, bool loop, float width, float aa_width)
	{
		add_chain(ps, loop, width, aa_width);
	}
	size_t inst_add_end()
	{
		size_t last = inst_objs.empty() ? 0 : inst_objs.back().first + inst_objs.back().second;
		size_t cur = data_f.size() / 7;
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
