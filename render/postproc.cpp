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
	
	void start(TimeSpan)
	{
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
	}
	void finish()
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
};
Postproc* Postproc::create_main_chain()
{
	return new PostprocMain;
}
