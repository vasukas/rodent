#include <future>
#include "client/resbase.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "ren_aal.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "shader.hpp"
#include "texture.hpp"

#define USE_NORM_INT 0 // caused bugs on old AMD card



struct Noise
{
	static const int tex_depth = 16;
	
	GLA_Texture tex;
	float t = 0.f;
	
	std::optional <std::future <std::pair <std::vector <uint8_t>, int>>> tex_async;
	
	Noise()
	{
		tex.target = GL_TEXTURE_3D;
		
		tex_async = std::async(std::launch::async, []
		{
			double k = 0.13;
			const int tex_size = 16 / k;
			
			std::vector<uint8_t> img_px( tex_size * tex_size * tex_depth * 3 );
			TimeSpan time0 = TimeSpan::since_start();
			
			for (int i = 0; i < tex_size * tex_size * tex_depth; ++i)
			{
				double x = k * (i / (tex_size * tex_size) * 2);
				double y = k * (i / tex_size % tex_size);
				double z = k * (i % tex_size);
				
	#define noise(x,y,z) perlin_noise_oct(x,y,z, 0.5, 2.0, 2)
	//#define noise(x,y,z) perlin_noise(x,y,z)
				double H = noise(x, y, z) * 0.5 + 0.5;
				double v = noise(x, y, z + 16.) * 0.5 + 0.5;
	#undef noise
				H = lerp(170, 210, H); // 0 - 360
				v = lerp(15, 30, v); // 0 - 100
				double S = 100; // 0 - 100
				
				int hi = H / 60;
				double vm = (100. - S) * v / 100.;
				double a = (v - vm) * (H - hi * 60) / 60.;
				double vi = vm + a;
				double vd = v - a;
				v *= 2.55;
				vi *= 2.55;
				vd *= 2.55;
				
				double r, g, b;
				if		(hi == 0) {r = v;  g = vi; b = vm;}
				else if (hi == 1) {r = vd; g = v;  b = vm;}
				else if (hi == 2) {r = vm; g = v;  b = vi;}
				else if (hi == 3) {r = vm; g = vd; b = v;}
				else if (hi == 4) {r = vi; g = vm; b = v;}
				else if (hi == 5) {r = v;  g = vm; b = vd;}
				else r = g = b = 255.;
				
				img_px[i*3+0] = r;
				img_px[i*3+1] = g;
				img_px[i*3+2] = b;
			}
			
			VLOGI("RenAAL generation time: {:.3f} seconds", (TimeSpan::since_start() - time0).seconds());
			
			return std::make_pair(std::move(img_px), tex_size);
		});
	}
	void setup(Shader* sh, TimeSpan passed)
	{
		glActiveTexture(GL_TEXTURE1);
		tex.bind();
		
		if (tex_async && tex_async->wait_for (std::chrono::seconds(0)) == std::future_status::ready)
		{
			auto res = tex_async->get();
			tex_async.reset();
			
			tex.bind();
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, res.second, res.second, tex_depth, 0, GL_RGB, GL_UNSIGNED_BYTE, res.first.data());
			
			tex.set_byte_size( res.second * res.second * tex_depth * 3 );
		}
		
		sh->set1f("t", t / tex_depth * 1.3);
		t += passed.seconds();
		
//		auto& cam = RenderControl::get().get_world_camera()->get_state();
		sh->set2f("offset", {});
		auto ssz = RenderControl::get().get_size();
		sh->set2f("scrk", float(ssz.x) / ssz.y, 1);
	}
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
#if USE_NORM_INT
	std::vector<uint8_t> data_i;
#endif

	size_t objs_off = 0; ///< Last vertex count
	std::vector<Obj> objs;
	
	Shader* sh;
	Shader* sh_inst;
	uint32_t prev_clr = 0;
	
	GLA_Texture tex;
	
	// for instanced drawing
	GLA_VertexArray inst_vao;
	std::vector<std::pair<size_t, size_t>> inst_objs;
	std::vector<InstObj> inst_q;
	
	// for grid
	GLA_Framebuffer fbo;
	GLA_Texture fbo_clr;
	Shader* fbo_sh;
	RAII_Guard fbo_g;
	Noise fbo_noi;
	
	
	
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
		
#if USE_NORM_INT
#define PI(x) data_i.push_back( norm_i8(x) )
#else
#define PI PF
#endif
		
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
		
#if USE_NORM_INT
		size_t dsz = ps.size() * 6 * 4;
#else
		size_t dsz = ps.size() * 6 * 7;
#endif
		if (4096 > dsz) dsz = 4096;
		reserve_more_block(objs, 1024);
		reserve_more_block(data_f, dsz);
#if USE_NORM_INT
		reserve_more_block(data_i, dsz);
#endif

		size_t n = ps.size();
		if (loop) ++n;
		for (size_t i = 1; i < n; ++i)
			add_line(ps[i%ps.size()], ps[i-1], width, wpar, aa_width);
		
		return n;
	}
	
	
	
	RenAAL_Impl()
	{
#if USE_NORM_INT
		vao.set_buffers({
			std::make_shared<GLA_Buffer>(4, GL_FLOAT, false, GL_STREAM_DRAW),
		    std::make_shared<GLA_Buffer>(3, GL_BYTE,  true,  GL_STREAM_DRAW)
		});
#else
		auto buf = std::make_shared<GLA_Buffer>(0);
		vao.set_attribs({ {buf, 4}, {buf, 3} });
#endif
		sh      = RenderControl::get().load_shader("aal");
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
		
		// for grid
		
		fbo_sh = RenderControl::get().load_shader("pp/aal_grid", [](Shader& sh){ sh.set1i("noi", 1); });
		fbo_g = RenderControl::get().add_size_cb([this]{ fbo_clr.set(GL_RGBA, RenderControl::get_size(), 0, 4); }, true);

		fbo.bind();
		fbo.attach_tex(GL_COLOR_ATTACHMENT0, fbo_clr);
	}
	void draw_line(vec2fp p0, vec2fp p1, uint32_t clr, float width, float aa_width, float clr_mul)
	{
		if (!clr) return;
		if (aa_width < 1) aa_width = 1;
		width += aa_width;
		float wpar = width / aa_width;
		
		reserve_more_block(objs, 1024);
		reserve_more_block(data_f, 4096);
#if USE_NORM_INT
		reserve_more_block(data_i, 4096);
#endif
		
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
#if USE_NORM_INT
			vao.bufs[1]->update( data_i.size(), data_i.data() );
#endif
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
#if USE_NORM_INT
			data_i.clear();
#endif
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
				auto cs = cossin_ft(o.tr.rot);
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
		
		// draw to buffer
		
		fbo.bind();
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
		glBlendEquation(GL_MAX);
		
		inst_vao.bind();
		
		Camera* cam = RenderControl::get().get_world_camera();
		const float *mx = cam->get_full_matrix();
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
		fbo_noi.setup(fbo_sh, RenderControl::get().get_passed());
		
		glActiveTexture(GL_TEXTURE0);
		fbo_clr.bind();
		
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	
	
	
	void inst_begin()
	{
#if USE_NORM_INT
		inst_vao.set_buffers({
			std::make_shared<GLA_Buffer>(4, GL_FLOAT, false, GL_STATIC_DRAW),
		    std::make_shared<GLA_Buffer>(3, GL_BYTE,  true,  GL_STATIC_DRAW)
		});
#else
		auto buf = std::make_shared<GLA_Buffer>(0);
		inst_vao.set_attribs({ {buf, 4}, {buf, 3} });
#endif
		inst_objs.clear();
	}
	void inst_end()
	{
		inst_vao.bufs[0]->update( data_f.size(), data_f.data() );
		data_f.clear(); data_f.shrink_to_fit();

#if USE_NORM_INT
		inst_vao.bufs[1]->update( data_i.size(), data_i.data() );
		data_i.clear(); data_i.shrink_to_fit();
#endif
	}
	void inst_add(const std::vector<vec2fp>& ps, bool loop, float width, float aa_width)
	{
		add_chain(ps, loop, width, aa_width);
	}
	size_t inst_add_end()
	{
		size_t last = inst_objs.empty() ? 0 : inst_objs.back().first + inst_objs.back().second;
#if USE_NORM_INT
		size_t cur = data_f.size() / 4;
#else
		size_t cur = data_f.size() / 7;
#endif
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
