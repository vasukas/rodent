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



class PostprocMain : public Postproc
{
public:
	const bool use_add = true;
	const bool use_after = false;
	
	Shader *aft_sh;
	GLA_Framebuffer aft_fb[2];
	GLA_Texture aft_tex[2];
	
	const float aft_feedback = 0.07;
	const float aft_alpha = 0.2;
	
	
	
	PostprocMain()
	{
		if (use_after)
		{
			aft_sh = RenderControl::get().load_shader("pp/pass");
			vec2i ssz = RenderControl::get_size();
			
			for (int i=0; i<2; ++i)
			{
				aft_tex[i].set(GL_RGBA16F, ssz);
				aft_fb[i].attach_tex(GL_COLOR_ATTACHMENT0, aft_tex[i]);
				
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
					LOG_THROW_X("PostprocMain::() afterglow FBO error {}", i);
			}
		}
	}
	void start(TimeSpan)
	{
		if (use_after)
		{
			aft_fb[0].bind();
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		if (use_add)
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
	}
	void finish()
	{
		if (use_after)
		{
			RenderControl::get().ndc_screen2().bind();
			
			aft_sh->bind();
			
			glActiveTexture(GL_TEXTURE0);
			aft_tex[1].bind();
			
			aft_sh->set1f("alpha", aft_feedback);
			glDrawArrays(GL_TRIANGLES, 0, 6);
			
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			
			aft_sh->set1f("alpha", aft_alpha);
			glDrawArrays(GL_TRIANGLES, 0, 6);
			
			aft_tex[0].bind();
			aft_sh->set1f("alpha", 1.f);
			glDrawArrays(GL_TRIANGLES, 0, 6);
			
			aft_fb [0].swap(aft_fb [1]);
			aft_tex[0].swap(aft_tex[1]);
		}
		
		if (use_add)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
};
Postproc* Postproc::create_main_chain()
{
	return new PostprocMain;
}
