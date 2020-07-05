#include <deque>
#include <future>
#include "core/settings.hpp"
#include "vaslib/vas_log.hpp"
#include "postproc.hpp"
#include "pp_graph.hpp"

#include "camera.hpp"
#include "ren_aal.hpp"
#include "ren_imm.hpp"
#include "ren_light.hpp"
#include "ren_particles.hpp"

#include "core/hard_paths.hpp"
#include "utils/res_image.hpp"

extern "C" {
#include "noise1234.h"
}



static std::vector<float> gauss_kernel(int n, float a)
{
	int size = n*2+1;
	float em = -1 / (2 * a * a);
	float cm = 1 / sqrt(2 * M_PI * a * a);
	float s = 0;
	
	std::vector<float> ker(size);
	for (int i=0; i<size; i++) {
		int d = i - n;
		ker[i] = exp(d*d * em) * cm;
		s += ker[i];
	}
	
	for (int i=0; i<size; i++) ker[i] /= s;
	return ker;
}



struct PPF_Bleed : PP_Filter
{
	int n = 1;
	
	PPF_Bleed()
	{
		sh = Shader::load("pp/bleed", {});
	}
	bool is_ok_int() override
	{
		return n != 0;
	}
	void proc() override
	{
		vec2fp d = {0.04, 0.08}; // meters
		d /= RenderControl::get().get_world_camera().coord_size();
		
		sh->bind();
		sh->set2f("disp", d);
		for (int i=0; i<n; ++i) draw(i == n-1);
	}
};



struct PPF_Tint : PP_Filter
{
	struct Step
	{
		float t, tps;
		FColor m0, m1;
		FColor a0, a1;
	};
	std::deque<Step> steps;
	FColor p_mul = FColor(1,1,1,1); // current
	FColor p_add = FColor(0,0,0,0);
	
	PPF_Tint()
	{
		sh = Shader::load("pp/tint", {});
	}
	bool is_ok_int() override
	{
		if (!steps.empty()) return true;
		for (int i=0; i<4; ++i) if (!aequ(p_mul[i], 1.f, 1/256.f)) return true;
		for (int i=0; i<4; ++i) if (!aequ(p_add[i], 0.f, 1/256.f)) return true;
		return false;
	}
	void proc() override
	{
		float time = get_passed().seconds();
		while (!steps.empty())
		{
			auto& s = steps.front();
			
			s.t += s.tps * time;
			if (s.t < 1.f)
			{
				p_mul = lerp(s.m0, s.m1, s.t);
				p_add = lerp(s.a0, s.a1, s.t);
				break;
			}
			time -= (s.t - 1.f) / s.tps;
			
			p_mul = s.m1;
			p_add = s.a1;
			steps.pop_front();
		}
		
		sh->bind();
		sh->set_clr("mul", p_mul);
		sh->set_clr("add", p_add);
		draw(true);
	}
	void reset()
	{
		steps.clear();
	}
	void add_step(TimeSpan ttr, FColor tar_mul, FColor tar_add)
	{
		FColor m0, a0;
		if (steps.empty()) {
			m0 = p_mul;
			a0 = p_add;
		}
		else {
			m0 = steps.back().m1;
			a0 = steps.back().a1;
		}
			
		auto& ns = steps.emplace_back();
		ns.t = 0;
		ns.tps = 1.f / std::max(ttr, TimeSpan::micro(1)).seconds();
		ns.m0 = m0;
		ns.a0 = a0;
		ns.m1 = tar_mul;
		ns.a1 = tar_add;
	}
};



struct PPF_Shake : PP_Filter
{
	const float decr_spd = 1. / 0.7; // per second
	const float max_len = 1.5; // seconds
	const vec2fp spd_mul = {12, 18};
	float t = 0, str = 0, str_tar = 0;
	
	PPF_Shake()
	{
		sh = Shader::load("pp/shake", {});
	}
	bool is_ok_int() override
	{
		return (str > 0 || str_tar > 0) && AppSettings::get().cam_pp_shake_str > 1e-5;
	}
	void proc() override
	{
		float ps = RenderControl::get().get_passed().seconds();
		
		if (str < str_tar)
		{
			float dt = (str_tar - str) * ps * 10;
			if (dt < 0.1)
			{
				str = str_tar;
				str_tar = 0;
			}
			else str += dt;
		}
		else str -= ps * decr_spd;
		t += ps;
		
		vec2i sz = RenderControl::get_size();
		float k = std::min(str, 2.f) * AppSettings::get().cam_pp_shake_str;
		float x = cossin_lut(t * spd_mul.x).y * k / sz.xy_ratio();
		float y = cossin_lut(t * spd_mul.y).y * k * 0.7;
		
		sh->bind();
		sh->set2f("tmod", x, y);
		draw(true);
	}
	void add(float power)
	{
		if (str <= 0) t = 0;
		str_tar = std::min(str + power, max_len * decr_spd);
	}
};



class PP_Bloom : public PP_Node
{
public:
	static constexpr float blur_a = 12.f; // sigma
	static constexpr int blur_n = 3;
	
	PP_Bloom(std::string name)
	    : PP_Node(std::move(name))
	{
		rsz_g = RenderControl::get().add_size_cb([this]
		{
			for (int i=0; i<2; ++i)
			{
				tex_s[i].set(GL_RGBA, RenderControl::get_size(), 0, 4);
				fbo_s[i].attach_tex(GL_COLOR_ATTACHMENT0, tex_s[i]);
			}
		});
		
		Shader::Callbacks cbs;
		cbs.pre_build = [](Shader& sh) {
			sh.set_def("KERN_SIZE", std::to_string(blur_n*2 + 1));
		};
		cbs.post_build = [](Shader& sh) {
			auto ker = gauss_kernel(blur_n, blur_a);
			ker[blur_n] = 1;
		    
		    sh.set1i("size", blur_n);
		    sh.setfv("mul", ker.data(), ker.size());
		};
		sh_blur = Shader::load("pp/blur", std::move(cbs));
		
		sh_mix = Shader::load("pp/pass", {});
	}
	
private:
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	RAII_Guard rsz_g;
	std::unique_ptr<Shader> sh_blur, sh_mix;
	
	bool prepare() override
	{
		if (!sh_blur->is_ok() || !sh_mix->is_ok())
			return false;
		
		for (auto& fbo : fbo_s)
		{
			fbo.bind();
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		return true;
	}
	void proc(GLuint output_fbo) override
	{
		glBlendFunc(GL_ONE, GL_ONE);
		glBlendEquation(GL_FUNC_ADD);
		
		glActiveTexture(GL_TEXTURE0);
		RenderControl::get().ndc_screen2().bind();
		
		sh_blur->bind();
		sh_blur->set2f("scr_px_size", RenderControl::get_size());
		
		// 1st pass
		
		fbo_s[1].bind();
		tex_s[0].bind();
		sh_blur->set1i("horiz", 1);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		fbo_s[0].bind();
		tex_s[1].bind();
		sh_blur->set1i("horiz", 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		// 2nd pass
		
		fbo_s[1].bind();
		tex_s[0].bind();
		sh_blur->set1i("horiz", 1);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
		
		glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
		tex_s[1].bind();
		sh_blur->set1i("horiz", 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	GLuint get_input_fbo() override {return fbo_s[0].fbo;}
};



class PP_Smoke : public PP_Node
{
public:
	bool enabled = true;	

	const int size = 256, z_size = 16;
	const int octs = 5;
	const float z_speed = 0.2;
	const float world_size = 25;
	
	const int circ_size = 64; // circle texture size
	const float y_rad_k = 0.8; // y-axis scale
	
	const float max_dist = 60;
	
	struct SmokeData {
		vec2fp ctr, vel;
		float et, lt, ft; // timeouts (seconds): expand, life, fade
		float rad, dt_rad; // radius, increase per second
		float a, in_a, dt_a; // alpha, increase,decrease per second
	};
	
	PP_Smoke(std::string name)
		: PP_Node(std::move(name))
	{
		TimeSpan time0 = TimeSpan::current();
		
		sh_mask = Shader::load("pp/smoke_mask", {});
		sh_eff = Shader::load("pp/smoke", {});
		sh_space_bg = Shader::load("space_bg", {});
		
		fbo_eff.em_texs.emplace_back();
		fbo_mask.em_texs.emplace_back();
		rsz_g = RenderControl::get().add_size_cb([this]
		{
			fbo_eff.em_texs[0].set(GL_RGBA, RenderControl::get_size(), 0, 4);
			fbo_eff.attach_tex(GL_COLOR_ATTACHMENT0, fbo_eff.em_texs[0]);
			fbo_eff.check_throw("PP_Smoke eff");
			
			fbo_mask.em_texs[0].set(GL_R8, RenderControl::get_size(), 0, 1);
			fbo_mask.attach_tex(GL_COLOR_ATTACHMENT0, fbo_mask.em_texs[0]);
			fbo_mask.check_throw("PP_Smoke mask");
		});
		
		//
		
		auto px = ImageInfo::procedural(HARDPATH_SMOKE_PROCIMG, {size, size * z_size}, ImageInfo::FMT_ALPHA,
		[&](ImageInfo& img)
		{
			const float freq = std::pow(2, -octs);
			const float z_freq = std::max(freq, 1.f / z_size);
		          
			std::vector<std::future<void>> fs;
			for (int z=0; z<z_size; ++z) {
				fs.emplace_back(std::async(std::launch::async, [&, z]{
					int i = z * (size * size);
					for (int y=0; y<size; ++y)
					for (int x=0; x<size; ++x)
					{
						float v = 0;
						for (int i=0, k=1; i<5; ++i, k*=2)
							v += (1.f/k) * pnoise3(x*k*freq, y*k*freq, z*k*z_freq, size*freq*k, size*freq*k, z_size*z_freq*k);
						img.raw()[i++] = 255 * clampf_n(0.5 + 0.5 * v);
					}
				}));
			}
		});
		
		tex_noi.target = GL_TEXTURE_3D;
		tex_noi.bind();
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, size, size, z_size, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
		tex_noi.set_byte_size(size * size * z_size);
		
		//
		
		px.resize(circ_size * circ_size);
		for (int y=0; y<circ_size; ++y)
		for (int x=0; x<circ_size; ++x)
		{
			float xd = float(x - circ_size/2) / (circ_size/2);
			float yd = float(y - circ_size/2) / (circ_size/2);
			float d = std::sqrt(xd*xd + yd*yd);
			d = std::pow(d, 2);
			px[y*circ_size + x] = 255 * (1 - std::min(1.f, d));
		}
		
		tex_circ.set(GL_R8, {circ_size, circ_size}, 0, 1, px.data(), GL_RED);
		
		VLOGI("Generated smoke in {:.3f} seconds", (TimeSpan::current() - time0).seconds());
	}
	void add(const Postproc::Smoke& i)
	{
		auto& s = smokes.emplace_back();
		s.ctr = i.at;
		s.vel = i.vel;
		s.et = i.et.seconds();
		s.lt = i.lt.seconds();
		s.ft = i.ft.seconds();
		s.rad = i.expand ? 0 : i.radius;
		s.dt_rad = i.expand ? i.radius / s.et : 0;
		s.a = 0;
		s.in_a = i.alpha / s.et;
		s.dt_a = i.alpha / s.ft;
	}
	void draw_space_background(float& alpha, bool enabled)
	{
		float passed = RenderControl::get().get_passed().seconds();
		float tti = 0.05; // time to fully switch, seconds
		float ttd = 0.3;
		alpha = enabled ? std::min(1.f, alpha + passed / tti) : std::max(0.f, alpha - passed / ttd);
		
		float sk = RenderControl::get_size().xy_ratio();
		t_val += z_speed * passed;
		
		sh_space_bg->bind();
		sh_space_bg->set4f("ps", 0.6, 1/sk, t_val, alpha);
		
		glActiveTexture(GL_TEXTURE0);
		tex_noi.bind();
		
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	
private:
	GLA_Framebuffer fbo_mask, fbo_eff;
	GLA_Texture tex_noi, tex_circ;
	std::unique_ptr<Shader> sh_mask, sh_eff, sh_space_bg;
	RAII_Guard rsz_g;
	
	std::vector<SmokeData> smokes;
	float t_val = 0;
	
	bool prepare() override
	{
		if (!sh_mask->is_ok() || !sh_eff->is_ok() || smokes.empty() || !enabled)
			return false;
		
		fbo_eff.bind();
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		return true;
	}
	void proc(GLuint output_fbo) override
	{
		auto& cam = RenderControl::get().get_world_camera();
		auto  ssz = RenderControl::get().get_size();
		
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
		
		fbo_mask.bind();
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		sh_mask->bind();
		sh_mask->set4mx("proj", cam.get_full_matrix());
		
		glActiveTexture(GL_TEXTURE0);
		tex_circ.bind();
		RenderControl::get().ndc_screen2().bind();
		
		const float z_max_dist = ssz.minmax().y + max_dist;
		const float time_mul = RenderControl::get().get_passed().seconds();
		t_val += z_speed * time_mul;
		
		for (auto it = smokes.begin(); it != smokes.end(); )
		{
			auto& s = *it;
			s.ctr += s.vel * time_mul;
			if (s.et > 0) {
				s.et -= time_mul;
				s.rad += s.dt_rad * time_mul;
				s.a += s.in_a * time_mul;
			}
			else if (s.lt > 0) {
				s.lt -= time_mul;
			}
			else if (s.ft > 0) {
				s.a -= s.dt_a * time_mul;
			}
			else {
				it = smokes.erase(it);
				continue;
			}
			
			float max = s.rad + z_max_dist;
			if (s.ctr.dist_squ(cam.get_state().pos) > max*max) {
				it = smokes.erase(it);
				continue;
			}
			++it;
			
			sh_mask->set4f("pars", s.ctr.x, s.ctr.y, s.rad, s.rad * y_rad_k);
			sh_mask->set1f("alpha", s.a);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		
		//
		
		sh_eff->bind();
		sh_eff->set1i("smoke", 1);
		sh_eff->set1i("mask", 2);
		
		vec2fp scr_z = cam.coord_size();
		vec2fp cpos = cam.get_state().pos;
		cpos.y = -cpos.y;
		cpos /= world_size;
		
		sh_eff->set3f("offset", cpos.x, cpos.y, t_val);
		sh_eff->set2f("scrk", scr_z / vec2fp::one(world_size));
		
		//
		
		glActiveTexture(GL_TEXTURE2);
		fbo_mask.em_texs[0].bind();
		glActiveTexture(GL_TEXTURE1);
		tex_noi.bind();
		glActiveTexture(GL_TEXTURE0);
		fbo_eff.em_texs[0].bind();
		
		glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	GLuint get_input_fbo() override {return fbo_eff.fbo;}
};



class Postproc_Impl : public Postproc
{
public:	
	bool is_ui_mode = true;
	
	void ui_mode(bool enable) override
	{
		RenAAL::get().draw_grid = !enable;
		RenLight::get().enabled = !enable;
		RenParticles::get().enabled = !enable;
		if (smoke) smoke->enabled = !enable;
		is_ui_mode = enable;
	}
	
	
	
	PPF_Tint* tint = nullptr;
	
	void tint_reset() override
	{
		if (tint) tint->reset();
	}
	void tint_seq(TimeSpan time_to_reach, FColor target_mul, FColor target_add) override
	{
		if (tint) tint->add_step(time_to_reach, target_mul, target_add);
	}
	
	
	
	struct CaptureInfo
	{
		GLA_Framebuffer fbo;
		Texture* tex;
	};
	
	std::optional<CaptureInfo> capture;
	PPN_OutputScreen* display;
	PPN_InputDraw* draw_ui;
	
	void capture_begin(Texture* tex) override
	{
		capture = CaptureInfo{};
		capture->tex = tex;
		capture->fbo.attach_tex(GL_COLOR_ATTACHMENT0, tex->get_obj());
		capture->fbo.check_throw("Postproc:: capture");
		display->fbo = capture->fbo.fbo;
		draw_ui->enabled = false;
	}
	void capture_end() override
	{
		if (capture) {
			display->fbo = 0;
			draw_ui->enabled = true;
			capture.reset();
		}
	}
	
	
	
	PPF_Shake* shake = nullptr;
	
	void screen_shake(float power) override
	{
		if (shake) shake->add(power);
	}
	
	
	
	PP_Smoke* smoke = nullptr;
	PPN_InputDraw* space_bg = nullptr;
	float space_bg_level = 0;
	
	void add_smoke(const Smoke& s) override
	{
		if (smoke) smoke->add(s);
	}
	
	
	
	// deleter for all renderers
	std::vector<std::function<void()>> r_dels;
	
	Postproc_Impl(bool& ok)
	{
#define INIT(NAME) \
		try {\
			auto var = NAME::init();\
			VLOGI(#NAME " initialized");\
			r_dels.emplace_back([var]{ delete var; });\
		} catch (std::exception& e) {\
			VLOGE(#NAME " initialization failed: {}", e.what());\
			return;}
		
		INIT(RenAAL);
		INIT(RenImm);
		INIT(RenLight);
		INIT(RenParticles);
		//
		INIT(PP_Graph);
		
		//
		
		display = new PPN_OutputScreen;
		
		new PPN_InputDraw("grid", PPN_InputDraw::MID_NONE, [](auto fbo)
		{
			RenAAL::get().render_grid(fbo);
		});
		
		new PPN_InputDraw("aal", PPN_InputDraw::MID_NONE, [](auto fbo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenAAL::get().render();
		});
		
		new PPN_InputDraw("parts", PPN_InputDraw::MID_NONE, [](auto fbo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glBlendFunc(GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
			RenParticles::get().render();
		});
		
		new PPN_InputDraw("imm", PPN_InputDraw::MID_NONE, [](auto fbo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			RenImm::get().render(RenImm::DEFCTX_WORLD);
		});
		
		draw_ui = new PPN_InputDraw("ui", PPN_InputDraw::MID_NONE, [](auto fbo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			RenImm::get().render(RenImm::DEFCTX_UI);
		});
		
		new PPN_InputDraw("light", PPN_InputDraw::MID_DEPTH_STENCIL, [](auto)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			RenLight::get().render();
		});
		
		std::vector<std::unique_ptr<PP_Filter>> fts;
		{
			fts.emplace_back(new PPF_Bleed);
			new PPN_Chain("E1", std::move(fts), []
			{
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glBlendEquation(GL_FUNC_ADD);
			});
		}{
			new PP_Bloom("bloom");
		}{
			smoke = new PP_Smoke("smoke");
		}{
			fts.emplace_back(shake = new PPF_Shake);
			fts.emplace_back(tint  = new PPF_Tint);
			new PPN_Chain("post", std::move(fts), {});
		}
		
		if (smoke) {
			space_bg = new PPN_InputDraw("space_bg", PPN_InputDraw::MID_COLOR, [this](auto)
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glBlendEquation(GL_FUNC_ADD);
				smoke->draw_space_background(space_bg_level, is_ui_mode);
			},
			[this] {return is_ui_mode || space_bg_level > 0.f;});
		}
		
		auto& g = PP_Graph::get();
		
		g.connect("grid", "smoke", 1);
		g.connect("aal", "E1", 2);
		g.connect("imm", "display", 2);
		
		g.connect("parts", "bloom");
		g.connect("bloom", "smoke", 3);
		
		g.connect("smoke", "post", 1);
		g.connect("light", "post", 2);
		
		g.connect("E1", "smoke", 2);
		g.connect("post", "display", 1);
		
		g.connect("space_bg", "display", 0);
		g.connect("ui", "display", 3);
		
		ok = true;
	}
	~Postproc_Impl()
	{
		for (auto it = r_dels.rbegin(); it != r_dels.rend(); ++it)
			(*it)();
	}
	void render() override
	{
		RenImm::get().render_pre();
		PP_Graph::get().render();
		RenImm::get().render_post();
	}
	void render_reset() override
	{
		RenImm::get().render_post();
	}
};



static Postproc* rni;
Postproc& Postproc::get() {return *rni;}
Postproc* Postproc::init()
{
	bool ok = false;
	rni = new Postproc_Impl(ok);
	if (!ok) delete rni;
	return rni;
}
Postproc::~Postproc() {rni = nullptr;}
