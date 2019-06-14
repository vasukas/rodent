#include "vaslib/vas_log.hpp"
#include "control.hpp"
#include "postproc.hpp"
#include "shader.hpp"

class PP_Glow : public Postproc
{
public:
	GLuint fbo[2], tex[2];
	Shader* sh;
	float t = 0.f;
	
	const float k_norm = 0.2f;
	const float sigma = 4.f;
	const int passes = 1;
	const int size = 3; // up to 8
	
	PP_Glow();
	~PP_Glow();
	void start(TimeSpan passed);
	void finish();
};



PP_Glow::PP_Glow()
{
	vec2i scr = RenderControl::get_size();
	
	glGenFramebuffers(2, fbo);
	glGenTextures(2, tex);
	
	for (int i=0; i<2; ++i)
	{
		glBindTexture(GL_TEXTURE_2D, tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scr.x, scr.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		
		glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[i], 0);
		
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			LOG_THROW_X("PP_Glow::() failed");
	}
	
	sh = RenderControl::get().load_shader("glow", [this](Shader& sh)
	{
	    static const int max = 17;
	    const float s = sigma * sigma;
	    const float k = sqrt(2 * M_PI);
	     
	    float kern[max] = {};
	    float tot = 0.f;
	     
		for (int i = 0; i <= size; ++i)
		{
			float v = exp(-i*i / (2 * s)) / (s * k);
			kern[size - i] = kern[size + i] = v;
			tot += v;
		}
	
		tot = k_norm / tot;
		for (int i = 0; i < max; ++i)
			kern[i] *= tot;
		kern[size] = 1.f;
		
	    sh.set1i("size", size);
		sh.setfv("kern", kern, size*2 + 1);
	});
}
PP_Glow::~PP_Glow()
{
	glDeleteFramebuffers(2, fbo);
	glDeleteTextures(2, tex);
}
void PP_Glow::start(TimeSpan passed)
{
	glClearColor(0, 0, 0, 0);
	
	glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
	glClear(GL_COLOR_BUFFER_BIT);
	
	t += passed.seconds();
}
void PP_Glow::finish()
{
	sh->bind();
	sh->set1f("t", t);
	
	RenderControl::get().ndc_screen2().bind();
	glActiveTexture(GL_TEXTURE0);
	
	for (int i=0; i<passes; ++i)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindTexture(GL_TEXTURE_2D, tex[0]);
		
		sh->set1i("horiz", 1);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		if (i == passes - 1) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		else {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glBindTexture(GL_TEXTURE_2D, tex[1]);
		
		sh->set1i("horiz", 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
}



Postproc* Postproc::create_main_chain()
{
	return new PP_Glow;
}
