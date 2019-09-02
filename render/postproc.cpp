#include "core/vig.hpp"
#include "vaslib/vas_log.hpp"
#include "control.hpp"
#include "postproc.hpp"
#include "shader.hpp"
#include "texture.hpp"



struct PP_Filter
{
	bool enabled = false;
	TimeSpan passed;
	std::function<void(bool is_last)> draw; ///< Flips buffers
	Shader* sh = nullptr;
	
	virtual ~PP_Filter() = default;
	virtual bool is_ok() {return sh && sh->get_obj();}
	virtual void proc() = 0;
	virtual std::string descr() = 0;
	virtual void dbgm_opts() {}
	
	void set_tc_size() {
		auto sz = RenderControl::get_size();
		sh->set2f("tc_size", sz.x, sz.y);
	}
};
struct PPF_Kuwahara : PP_Filter
{
	float r = 4.f;
	bool dir = true;
	
	PPF_Kuwahara()
	{
		sh = RenderControl::get().load_shader( dir? "pp/kuwahara_dir" : "pp/kuwahara", {}, false );
	}
	void proc()
	{
		int xr = r, yr = r;
		sh->bind();
		sh->set1i("x_radius", xr);
		sh->set1i("y_radius", yr);
		sh->set1f("samp_mul", 1. / ((xr+1) * (yr+1)) );
		set_tc_size();
		
		draw(true);
	}
	std::string descr() {return FMT_FORMAT("Kuwahara; dir: {}, radius: {}", dir, r);}
};
struct PPF_Glowblur : PP_Filter
{
	const int n_max = 8; // same as in shader
	const float a = 12.f; // sigma
	
	int n = 3, n_old = -1;
	int pass = 1;
	
	PPF_Glowblur()
	{
		sh = RenderControl::get().load_shader("pp/gauss", [this](Shader&){ n_old = -1; }, false);
	}
	bool is_ok() {return PP_Filter::is_ok() && pass != 0;}
	void proc()
	{
		sh->bind();
		sh->set2f("scr_px_size", RenderControl::get_size());
		
		if (n_old != n)
		{
			if (n > n_max) n = n_max;
			n_old = n;
			
			int size = n*2+1;
			float em = -1 / (2 * a * a);
			float cm = 1 / sqrt(2 * M_PI * a * a);
			
			double s = 0;
			float ker[n_max*2+1];
			for (int i=0; i<=size; i++) {
				int d = i - n;
				ker[i] = exp(d*d * em) * cm;
				s += ker[i];
			}
			for (int i=0; i<size; i++) ker[i] /= s;
			ker[n] = 1;
			
			sh->set1i("size", n);
			sh->setfv("mul", ker, size);
		}
		for (int i=0; i<pass; ++i)
		{
			sh->set1i("horiz", 1);
			draw(false);
			
			sh->set1i("horiz", 0);
			draw(i == pass-1);
		}
	}
	std::string descr() {return FMT_FORMAT("Glowblur; passes: {}, n: {}", pass, n);}
	void dbgm_opts() {
		if (vig_button("N-")) {if (n) --n;}
		if (vig_button("N+")) ++n;
		if (vig_button("Ps-")) {if (pass) --pass;}
		if (vig_button("Ps+")) ++pass;
	}
};
struct PPF_Bleed : PP_Filter
{
	int n = 1;
	
	PPF_Bleed()
	{
		sh = RenderControl::get().load_shader("pp/bleed", {}, false);
	}
	bool is_ok() {return PP_Filter::is_ok() && n != 0;}
	void proc()
	{
		sh->bind();
		for (int i=0; i<n; ++i) draw(i == n-1);
	}
	std::string descr() {return FMT_FORMAT("Bleed; n: {}", n);}
	void dbgm_opts() {
		if (vig_button("N-")) {if (n) --n;}
		if (vig_button("N+")) ++n;
	}
};



struct PP_Chain
{
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	
	std::vector<std::unique_ptr<PP_Filter>> fts;
	bool bs_index, bs_last;
	
	RAII_Guard dbgm_g;
	RAII_Guard ren_rsz;
	std::string dbg_name;
	
	std::optional<GLint> prev_fbo;
	
	
	
	PP_Chain(std::vector<std::unique_ptr<PP_Filter>> fts_in, std::string dbg_name)
	    : fts(std::move(fts_in)), dbg_name(dbg_name)
	{
		auto set_fb = [this]
		{
			for (int i=0; i<2; ++i)
			{
				tex_s[i].set(GL_RGBA, RenderControl::get_size(), 0, 4);
				fbo_s[i].attach_tex(GL_COLOR_ATTACHMENT0, tex_s[i]);
			}
		};
		set_fb();
		ren_rsz = RenderControl::get().add_size_cb(set_fb);
		
		for (auto& f : fts) f->draw = [this](bool is_last)
		{
			if (is_last && bs_last) glBindFramebuffer(GL_FRAMEBUFFER, 0);
			else {
				fbo_s[bs_index].bind();
				glClear(GL_COLOR_BUFFER_BIT);
			}
			
			tex_s[!bs_index].bind();			
			bs_index = !bs_index;
			
			glDrawArrays(GL_TRIANGLES, 0, 6);
		};
		
		dbgm_g = vig_reg_menu(VigMenu::DebugRenderer, 
		[this]{
			vig_label_a("==== {} ====\n", this->dbg_name);
			for (auto& f : fts) {
				bool ok = f->is_ok();
				bool en = f->enabled && ok;
				vig_checkbox(en, f->descr() + (ok? ". [OK]" : ". [Err]"));
				if (ok) f->enabled = en;
				f->dbgm_opts();
				vig_lo_next();
			}
		});
	}
	void start(TimeSpan passed)
	{
		bool any = false;
		for (auto& f : fts) if (f->enabled && f->is_ok()) {any = true; break;}
		
		if (any)
		{
			prev_fbo = 0;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo.value());
			
			fbo_s[0].bind();
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
			
			for (auto& f : fts) f->passed = passed;
		}
	}
	void finish()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, *prev_fbo);
		prev_fbo.reset();
	}
	void render()
	{
		int last = fts.size() - 1;
		for (; last != -1; --last) if (fts[last]->enabled && fts[last]->is_ok()) break;
		
		if (last != -1)
		{
			glActiveTexture(GL_TEXTURE0);
			RenderControl::get().ndc_screen2().bind();
		}
		
		bs_index = true;
		for (int i=0; i<=last; ++i)
		{
			auto& f = fts[i];
			if (f->enabled && f->is_ok())
			{
				bs_last = (i == last);
				f->proc();
			}
		}
	}
};



class PostprocMain : public Postproc
{
public:
	std::array<std::unique_ptr<PP_Chain>, CI_TOTAL_COUNT_INTERNAL> cs;
	
	PostprocMain()
	{
		std::vector<std::unique_ptr<PP_Filter>> fts;
		{	
			fts.emplace_back(new PPF_Kuwahara)->enabled = false;
			fts.emplace_back(new PPF_Bleed)->enabled = true;
			cs[CI_MAIN].reset(new PP_Chain (std::move(fts), "PP main"));
		}{
			fts.emplace_back(new PPF_Glowblur)->enabled = true;
			cs[CI_PARTS].reset(new PP_Chain (std::move(fts), "PP parts"));
		}
	}
	void start(TimeSpan passed, ChainIndex i)
	{
		if (cs[i]) cs[i]->start(passed);
	}
	void finish(ChainIndex i)
	{
		if (cs[i]) cs[i]->finish();
	}
	void render(ChainIndex i)
	{
		if (cs[i]) cs[i]->render();
	}
};
Postproc* Postproc::create_main_chain()
{
	return new PostprocMain;
}
