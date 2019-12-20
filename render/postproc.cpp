#include <deque>
#include "core/settings.hpp"
#include "vaslib/vas_log.hpp"
#include "postproc.hpp"
#include "pp_graph.hpp"

#include "ren_aal.hpp"
#include "ren_imm.hpp"
#include "ren_particles.hpp"



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
		sh->bind();
		for (int i=0; i<n; ++i) draw(i == n-1);
	}
};



struct PPF_Tint : PP_Filter
{
	struct Step
	{
		float t, tps;
		FColor mul, add;
	};
	std::deque<Step> steps;
	FColor p_mul = FColor(1,1,1,1);
	FColor p_add = FColor(0,0,0,0);
	
	PPF_Tint()
	{
		sh = Shader::load("pp/tint", {});
	}
	bool is_ok_int() override
	{
		return !steps.empty();
	}
	void proc() override
	{
		TimeSpan time = get_passed();
		while (!steps.empty())
		{
			auto& s = steps.front();
			float incr = s.tps * time.seconds();
			
			float left = 1 - s.t;
			if (incr < left)
			{
				s.t += incr;
				break;
			}
			
			float used = left / incr;
			time *= 1 - used;
			
			p_mul = s.mul;
			p_add = s.add;
			steps.pop_front();
		}
		
		sh->bind();
		auto [mul, add] = calc_state();
		sh->set_clr("mul", mul);
		sh->set_clr("add", add);
		draw(true);
	}
	void reset()
	{
		std::tie(p_mul, p_add) = calc_state();
		steps.clear();
	}
	void add_step(TimeSpan ttr, FColor tar_mul, FColor tar_add)
	{
		auto& ns = steps.emplace_back();
		ns.t = 0;
		ns.tps = 1.f / std::max(ttr, TimeSpan::fps(10000)).seconds();
		ns.mul = tar_mul;
		ns.add = tar_add;
	}
	std::pair<FColor, FColor> calc_state()
	{
		if (steps.empty()) return {p_mul, p_add};
		auto& s = steps.back();
		float t = std::min(1.f, s.t);
		return {lerp(p_mul, s.mul, t), lerp(p_add, s.add, t)};
	}
};



struct PPF_Shake : PP_Filter
{
	float t = 0, str = 0;
	
	PPF_Shake()
	{
		sh = Shader::load("pp/shake", {});
	}
	bool is_ok_int() override
	{
		return str > 0 && AppSettings::get().cam_pp_shake_str > 1e-5;
	}
	void proc() override
	{
		vec2i sz = RenderControl::get_size();
		float k = std::min(str, 2.f) * AppSettings::get().cam_pp_shake_str;
		float x = cossin_lut(t * 10).y * k / sz.xy_ratio();
		float y = cossin_lut(t * 15).y * k * 0.7;
		
		float ps = RenderControl::get().get_passed().seconds();
		t += ps;
		str -= ps / 0.7;
		
		sh->bind();
		sh->set2f("tmod", x, y);
		draw(true);
	}
	void add(float power)
	{
		if (str <= 0) t = 0.01;
		str = std::min(str + power, 5.f);
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
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
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
		
		glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
		tex_s[1].bind();
		sh_blur->set1i("horiz", 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		//
		
		glBlendFunc(GL_ONE, GL_ONE);
		glBlendEquation(GL_FUNC_ADD);
	}
	GLuint get_input_fbo() override {return fbo_s[0].fbo;}
};



class Postproc_Impl : public Postproc
{
public:	
	PPF_Tint* tint = nullptr;
	
	void tint_reset()
	{
		if (tint) tint->reset();
	}
	void tint_seq(TimeSpan time_to_reach, FColor target_mul, FColor target_add)
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
	
	void capture_begin(Texture* tex)
	{
		capture = CaptureInfo{};
		capture->tex = tex;
		capture->fbo.attach_tex(GL_COLOR_ATTACHMENT0, tex->get_obj());
		capture->fbo.check_throw("Postproc:: capture");
		display->fbo = capture->fbo.fbo;
		draw_ui->enabled = false;
	}
	void capture_end()
	{
		if (capture) {
			display->fbo = 0;
			draw_ui->enabled = true;
			capture.reset();
		}
	}
	
	
	
	PPF_Shake* shake;
	
	void screen_shake(float power)
	{
		if (shake) shake->add(power);
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
		INIT(RenParticles);
		//
		INIT(PP_Graph);
		
		//
		
		display = new PPN_OutputScreen;
		
		new PPN_InputDraw("grid", [](auto fbo)
		{
			RenAAL::get().render_grid(fbo);
		});
		
		new PPN_InputDraw("aal", [](auto fbo)
		{
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
			glBlendEquation(GL_MAX);
			
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenAAL::get().render();
		});
		
		new PPN_InputDraw("parts", [](auto fbo)
		{
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
			
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenParticles::get().render();
		});
		
		new PPN_InputDraw("imm", [](auto fbo)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenImm::get().render(RenImm::DEFCTX_WORLD);
		});
		
		draw_ui = new PPN_InputDraw("ui", [](auto fbo)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenImm::get().render(RenImm::DEFCTX_UI);
		});
		
		std::vector<std::unique_ptr<PP_Filter>> fts;
		{	
			fts.emplace_back(new PPF_Bleed);
			new PPN_Chain("E1", std::move(fts), []
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glBlendEquation(GL_FUNC_ADD);
			});
		}{
			new PP_Bloom("bloom");
		}{
			fts.emplace_back(shake = new PPF_Shake);
			fts.emplace_back(tint  = new PPF_Tint);
			new PPN_Chain("post", std::move(fts), {});
		}
		
		auto& g = PP_Graph::get();
		
		g.connect("grid", "post", 1);
		g.connect("aal", "E1", 2);
		g.connect("imm", "display", 2);
		
		g.connect("parts", "bloom");
		g.connect("bloom", "post", 3);
		
		g.connect("E1", "post", 2);
		g.connect("post", "display", 1);
		g.connect("ui", "display", 3);
		
		ok = true;
	}
	~Postproc_Impl()
	{
		for (auto it = r_dels.rbegin(); it != r_dels.rend(); ++it)
			(*it)();
	}
	void render()
	{
		RenImm::get().render_pre();
		PP_Graph::get().render();
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
