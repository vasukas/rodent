#include <deque>
#include "core/settings.hpp"
#include "postproc.hpp"
#include "pp_graph.hpp"

#include "particles.hpp"
#include "ren_aal.hpp"
#include "ren_imm.hpp"



struct PPF_Glowblur : PP_Filter
{
	const float a = 12.f; // sigma
	int n = 3, n_old = -1;
	int pass = 1;
	
	PPF_Glowblur()
	{
		sh = Shader::load_cb("pp/gauss", [this](Shader&){ n_old = -1; });
	}
	bool is_ok_int() override
	{
		return n > 0 && pass > 0;
	}
	void proc() override
	{
		sh->bind();
		sh->set2f("scr_px_size", RenderControl::get_size());
		
		if (n_old != n)
		{
			auto def = sh->get_def("KERN_SIZE");
			
			const int n_max = def ? std::atoi(def->value.c_str()) : 8;
			n_old = n = std::min(n, n_max);
			
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
			ker[n] = 1;
			
			sh->set1i("size", n);
			sh->setfv("mul", ker.data(), size);
		}
		for (int i=0; i<pass; ++i)
		{
			sh->set1i("horiz", 1);
			draw(false);
			
			sh->set1i("horiz", 0);
			draw(i == pass-1);
		}
	}
};


struct PPF_Bleed : PP_Filter
{
	int n = 1;
	
	PPF_Bleed()
	{
		sh = Shader::load("pp/bleed");
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
		FColor d_mul, d_add;
	};
	std::deque<Step> steps;
	FColor mul = FColor(1,1,1,1);
	FColor add = FColor(0,0,0,0);
	
	PPF_Tint()
	{
		sh = Shader::load_cb("pp/tint", [this](Shader& sh){
		     sh.set_clr("mul", mul);
		     sh.set_clr("add", add);
		});
	}
	bool is_ok_int() override
	{
		if (!steps.empty()) return true;
		for (int i=0; i<4; ++i)
		{
			if (!aequ(mul[i], 1.f, 0.001)) return true;
			if (!aequ(add[i], 0.f, 0.001)) return true;
		}
		return false;
	}
	void proc() override
	{
		TimeSpan time = get_passed();
		while (!steps.empty())
		{
			auto& s = steps.front();
			bool fin = false;
			
			float incr = s.tps * time.seconds();
			
			float left = 1 - s.t;
			if (incr >= left)
			{
				float used = left / incr;
				time *= 1 - used;
				
				incr = left;
				fin = true;
			}
			else s.t += incr;
			
			mul += FColor(s.d_mul).mul(incr);
			add += FColor(s.d_add).mul(incr);
			
			if (fin) steps.pop_front();
			else break;
		}
		
		sh->bind();
		sh->set_clr("mul", mul);
		sh->set_clr("add", add);
		draw(true);
	}
	void reset()
	{
		steps.clear();
	}
	void add_step(TimeSpan ttr, FColor tar_mul, FColor tar_add)
	{
		FColor m0 = mul, a0 = add;
		for (auto& s : steps) {
			m0 += s.d_mul;
			a0 += s.d_add;
		}
		
		auto& ns = steps.emplace_back();
		ns.t = 0;
		ns.tps = 1.f / ttr.seconds();
		ns.d_mul = tar_mul - m0;
		ns.d_add = tar_add - a0;
	}
};


class PP_Bloom : public PP_Node
{
public:
	std::function<bool()> is_enabled;
	
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
		}
		, true);
		
		sh_blur = Shader::load("pp/gauss", false, false);
		sh_blur->pre_reb = [this](Shader& sh)
		{
			sh.get_def("KERN_SIZE")->value = std::to_string(n*2+1);
		};
		sh_blur->on_reb = [this](Shader& sh)
		{
			int size = n*2+1;
			std::vector<float> ker(size);
			for (int i=0; i<size; i++) {
				int d = i - n;
				ker[i] = d? 1.f / (d * rad / n) : 1;
			}
			
			sh.set1i("size", n);
			sh.setfv("mul", ker.data(), size);
		};
		sh_blur->rebuild();
		
		sh_mix = Shader::load("pp/pass");
	}
	
private:
	const int n = 12; // 8
	const float rad = 3;
	
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	RAII_Guard rsz_g;
	
	Shader* sh_blur;
	Shader* sh_mix;
	
	bool prepare() override
	{
		if (is_enabled && !is_enabled())
			return false;
		
		if (!sh_blur->is_ok() || !sh_mix->is_ok())
			return false;
		
		for (int i=0; i<2; ++i)
		{
			fbo_s[i].bind();
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
		
		fbo_s[1].bind();
		tex_s[0].bind();
		sh_blur->bind();
		sh_blur->set2f("scr_px_size", RenderControl::get_size());
		sh_blur->set1i("horiz", 1);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
		tex_s[1].bind();
		sh_blur->set1i("horiz", 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		tex_s[0].bind();
		sh_mix->bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
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
	
	
	
	Postproc_Impl()
	{
		auto g = RenderControl::get().get_ppg();
		new PPN_OutputScreen;
		
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
			ParticleRenderer::get().render();
		});
		
		new PPN_InputDraw("imm", [](auto fbo)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			RenImm::get().render(RenImm::DEFCTX_WORLD);
		});
		
		new PPN_InputDraw("ui", [](auto fbo)
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
			fts.emplace_back(new PPF_Glowblur);
			auto ch = new PPN_Chain("E2", std::move(fts), []
			{
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
				glBlendEquation(GL_FUNC_ADD);
			});
			ch->is_enabled = []{
				return AppSettings::get().use_particles_pp;
			};
		}{
			fts.emplace_back(tint = new PPF_Tint);
			new PPN_Chain("post", std::move(fts), {});
		}{
			auto ch = new PP_Bloom("bloom");
			ch->is_enabled = []{
				return AppSettings::get().use_particles_bloom;
			};
		}
		
		g->connect("grid", "post", 1);
		g->connect("aal", "E1", 2);
		g->connect("imm", "display", 2);
		g->connect("parts", "E2");
		
		g->connect("E1", "post", 2);
//		g->connect("E2", "post", 3);
		g->connect("post", "display", 1);
		g->connect("ui", "display", 3);
		
		g->connect("E2", "bloom");
		g->connect("bloom", "post", 3);
	}
};



static Postproc* rni;
Postproc& Postproc::get() {return *rni;}
Postproc* Postproc::init() {return rni = new Postproc_Impl;}
Postproc::~Postproc() {rni = nullptr;}
