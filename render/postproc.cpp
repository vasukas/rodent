#include "vaslib/vas_log.hpp"
#include "control.hpp"
#include "postproc.hpp"
#include "shader.hpp"
#include "texture.hpp"



struct GLA_Framebuffer
{
	GLuint fbo = 0;
	
	GLA_Framebuffer() {
		glGenFramebuffers(1, &fbo);
	}
	~GLA_Framebuffer() {
		glDeleteFramebuffers(1, &fbo);
	}
	void bind() {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}
	void attach_tex(GLenum point, GLuint tex) {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, tex, 0);
	}
	void swap(GLA_Framebuffer& obj) {
		std::swap(fbo, obj.fbo);
	}
};



class PP_Filter
{
public:
	bool enabled = false;
	TimeSpan passed;
	std::function<void(bool is_last)> draw; ///< Flips buffers
	Shader* sh = nullptr;
	
	virtual ~PP_Filter() = default;
	virtual void proc() = 0;
	
	void set_tc_size() {
		auto sz = RenderControl::get_size();
		sh->set2f("tc_size", sz.x, sz.y);
	}
};
class PPF_Kuwahara : public PP_Filter
{
public:
	float r = 4.f;
	bool dir = true;
	
	PPF_Kuwahara()
	{
		sh = RenderControl::get().load_shader( dir? "pp/kuwahara_dir" : "pp/kuwahara");
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
};



class PostprocMain : public Postproc
{
public:
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	
	std::vector<std::unique_ptr<PP_Filter>> fts;
	bool bs_index, bs_last;
	
	
	PostprocMain()
	{
		for (int i=0; i<2; ++i)
		{
			tex_s[i].set(GL_RGBA, RenderControl::get_size());
			fbo_s[i].attach_tex(GL_COLOR_ATTACHMENT0, tex_s[i]);
		}
		
//		fts.emplace_back(new PPF_Kuwahara);
//		fts.back()->enabled = true;
		
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
	}
	void start(TimeSpan passed)
	{
		bool any = false;
		for (auto& f : fts) if (f->enabled) {any = true; break;}
		
		if (any)
		{
			fbo_s[0].bind();
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
			
			for (auto& f : fts) f->passed = passed;
		}
		
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
	}
	void finish()
	{
		int last = fts.size() - 1;
		for (; last != -1; --last) if (fts[last]->enabled) break;
		
		if (last != -1)
		{
			glActiveTexture(GL_TEXTURE0);
			RenderControl::get().ndc_screen2().bind();
		}
		
		bs_index = true;
		for (int i=0; i<=last; ++i)
		{
			auto& f = fts[i];
			if (f->enabled)
			{
				bs_last = (i == last);
				f->proc();
			}
		}
		
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
};
Postproc* Postproc::create_main_chain()
{
	return new PostprocMain;
}
