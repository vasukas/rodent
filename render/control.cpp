#include <unordered_map>
#include <SDL2/SDL.h>
#include "../settings.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "gl_utils.hpp"
#include "ren_imm.hpp"
#include "ren_text.hpp"
#include "shader.hpp"

class RenderControl_Impl;
static RenderControl_Impl* rct;



static bool gl_library_init()
{
	glewExperimental = true;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		VLOGE("glewInit failed - {}", glewGetErrorString(err));
		return false;
	}	
	VLOGI("glew version: {}", glewGetString(GLEW_VERSION));
	return true;
}



class RenderControl_Impl : public RenderControl
{
public:
	bool crit_error = false;
	bool reload_fail = false;
	
	SDL_Window* wnd = nullptr;
	SDL_GLContext glctx = nullptr;
	bool visib = true;
	
	int max_tex_size = 1024;
	Camera cam, cam_ui;
	
	struct ShaderInfo
	{
		std::unique_ptr <Shader> sh;
		std::function <void(Shader*)> reload_cb;
		bool is_crit;
	};
	std::unordered_map <std::string, ShaderInfo> shads;
	
	GLA_SingleVAO* ndc_screen2_obj = nullptr;
	
	RenImm* r_imm = nullptr;
	RenText* r_text = nullptr;
	
	
	
	RenderControl_Impl(bool& ok)
	{
		if (SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			VLOGE("SDL_InitSubSystem failed - {}", SDL_GetError());
			return;
		}
		
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		
		auto wnd_sz = AppSettings::get().wnd_size;
		wnd = SDL_CreateWindow( "Loading...", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, wnd_sz.x, wnd_sz.y, SDL_WINDOW_OPENGL );
		if (!wnd)
		{
			VLOGE("SDL_CreateWindow failed - {}", SDL_GetError());
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			return;
		}
		
		glctx = SDL_GL_CreateContext(wnd);
		if (!glctx)
		{
			VLOGE("SDL_GL_CreateContext failed - {}", SDL_GetError());
			return;
		}
		
		
		
		if (!gl_library_init())
		{
			VLOGE("gl_library_init() failed");
			return;
		}
		
		int maj, min;
		glGetIntegerv(GL_MAJOR_VERSION, &maj);
		glGetIntegerv(GL_MINOR_VERSION, &min);
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
		VLOGI("OpenGL {}.{}", maj, min);
		VLOGI("  Version:  {}", glGetString(GL_VERSION));
		VLOGI("  Vendor:   {}", glGetString(GL_VENDOR));
		VLOGI("  Renderer: {}", glGetString(GL_RENDERER));
		VLOGI("  Max texture size: {}", max_tex_size);
		
		
		
		if (glDebugMessageCallback)
		{
			struct FOO {
				static void GLAPIENTRY f(GLenum, GLenum type, GLuint, GLenum, GLsizei length, const GLchar* msg, const void*) {
//					if (sever == GL_DEBUG_SEVERITY_NOTIFICATION) return;
					if (type == GL_DEBUG_TYPE_ERROR) VLOGE("GL: {}", std::string_view(msg, length));
//					else VLOGV("GL ({}): {}", type, msg);
				}
			};
			
			while (glGetError()) ;
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback( &FOO::f, nullptr );
			
			if (glGetError() == GL_NO_ERROR)
				VLOGI("glDebugMessageCallback set");
			else {
				glDisable(GL_DEBUG_OUTPUT);
				VLOGE("glDebugMessageCallback set failed");
			}
		}
		else VLOGW("glDebugMessageCallback not available");
		
//		if (SDL_GL_SetSwapInterval(-1))
//		{
//			VLOGW("Enabling late vsync failed");
//			if (SDL_GL_SetSwapInterval(1)) VLOGW("Enabling vsync failed");
//			else VLOGI("Enabled vsync");
//		}
//		else VLOGI("Enabled late vsync");
		
		
//		glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(0);
		
		glEnable( GL_SCISSOR_TEST );
		
//		int msaa;
//		if (!SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &msaa) && msaa)
//		{
//			glEnable(GL_MULTISAMPLE);
//			VLOGI("MSAA enabled: {}x", msaa);
//		}
//		else VLOGI("MSAA disabled");
		
		
		
		ndc_screen2_obj = new GLA_SingleVAO;
		ndc_screen2_obj->set_attrib(0, 2);
		ndc_screen2_obj->bind_buf();
		
		const float ps[] = {
		    -1, -1,   1, -1,   -1, 1,
		              1, -1,   -1, 1,   1, 1
		};
		glBufferData( GL_ARRAY_BUFFER, 12 * sizeof(float), ps, GL_STATIC_DRAW );
		
		
		
		rct = this; // may be needed in these classes
		
		if (!RenText::init()) return;
		r_text = &RenText::get();
		
		if (!RenImm::init()) return;
		r_imm = &RenImm::get();
		
		{
			RenImm::Context cx;
			size_t cx_id;
			
			cx.cam = &cam;
			cx.sh      = load_shader("imm",      {}, true);
			cx.sh_text = load_shader("imm_text", {}, true);
			cx_id = RenImm::get().add_context(cx);
			
			cx.cam = &cam_ui;
			cx_id = RenImm::get().add_context(cx);
			
			ASSERT( cx_id == RenImm::DEFCTX_UI, "RenderControl::() code error" );
		}
		
		ok = true;
	}
	~RenderControl_Impl()
	{
		delete r_imm;
		delete r_text;
		
		shads.clear();
		delete ndc_screen2_obj;
	
		SDL_GL_DeleteContext( glctx );
		SDL_DestroyWindow( wnd );
		if (wnd) SDL_QuitSubSystem( SDL_INIT_VIDEO );
		
		rct = nullptr;
	}
	
	int     get_max_tex()      { return max_tex_size; }
	Camera* get_world_camera() { return &cam; }
	Camera* get_ui_camera()    { return &cam_ui; }
	bool    is_visible()       { return visib; }
	SDL_Window* get_wnd()      { return wnd; }
	
	bool render( TimeSpan passed )
	{
		if (crit_error) return false;
		if (reload_fail)
		{
			glClearColor(1, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			SDL_GL_SwapWindow(wnd);
			return true;
		}
		if (visib)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, get_size().x, get_size().y);
			
//			glClearColor(0.965, 0.937, 0.816, 0); // 247 239 208
//			glClearColor(0.863, 0.835, 0.725, 0); // 220 213 185
			glClearColor(0.0, 0.03, 0.07, 0);
			glClear(GL_COLOR_BUFFER_BIT);
			
			cam.   set_vport_full();
			cam_ui.set_vport_full();
			cam_ui.set_pos( cam_ui.get_vport().size() / 2 );
			
			RenImm::get().render();
			
			SDL_GL_SwapWindow(wnd);
		}
		cam.step( passed );
		return true;
	}
	void on_event( SDL_Event& ev )
	{
		if (ev.type == SDL_WINDOWEVENT)
		{
//			if (ev.window.windowID != SDL_GetWindowID(wnd)) return;
			
			if		(ev.window.type == SDL_WINDOWEVENT_HIDDEN ||
			         ev.window.type == SDL_WINDOWEVENT_MINIMIZED ) visib = false;
			else if (ev.window.type == SDL_WINDOWEVENT_SHOWN ||
			         ev.window.type == SDL_WINDOWEVENT_EXPOSED ) visib = true;
		}
		else if (ev.type == SDL_RENDER_DEVICE_RESET)
		{
			VLOGC( "RenderControl::on_event() device reset not supported" );
			crit_error = true;
		}
	}
	void set_vsync( bool on )
	{
		if (SDL_GL_SetSwapInterval( on? 1 : 0 ))
			VLOGW( "RenderControl::set_vsync() failed - {}", on );
		else
			VLOGI( "RenderControl::set_vsync() ok - {}", on );
	}
	bool has_vsync()
	{
		return 0 != SDL_GL_GetSwapInterval();
	}
	Shader* load_shader( const char *name, std::function <void(Shader*)> reload_cb, bool is_crit )
	{
		std::string n = name;
		
		auto it = shads.find( n );
		if (it == shads.end())
		{
			it = shads.emplace( n, ShaderInfo({ std::unique_ptr< Shader >( Shader::load(name) ), reload_cb, is_crit }) ).first;
			auto& s = it->second.sh;
			
			if (!s.get())
			{
				if (it->second.is_crit)
				{
					if (shader_fail) crit_error = true;
					reload_fail = true;
				}
				s.reset( new Shader (0) );
			}
			else if (reload_cb)
			{
				glUseProgram( s->get_obj() );
				reload_cb( s.get() );
			}
		}
		return it->second.sh.get();
	}
	void reload_shaders()
	{
		VLOGI("Reloading all shaders...");
		reload_fail = false;
		
		for (auto &p : shads)
		{
			auto& s = p.second.sh;
			auto n = Shader::load( p.first.data() );
			
			if (n)
			{
				*s = std::move( *n );
				delete n;
				
				if (p.second.reload_cb)
				{
					glUseProgram( s->get_obj() );
					p.second.reload_cb( s.get() );
				}
			}
			else if (s->get_obj())
			{
				*s = Shader( 0 );
				
				if (p.second.is_crit)
					reload_fail = true;
			}
		}
		
		if (reload_fail) VLOGW("Reload failed, renderer disabled");
	}
	GLA_SingleVAO* ndc_screen2()
	{
		return ndc_screen2_obj;
	}
};
bool RenderControl::init()
{
	if (!rct)
	{
		bool ok = false;
		rct = new RenderControl_Impl (ok);
		if (!ok)
		{
			VLOGE("RenderControl::init() failed");
			delete rct;
			return false;
		}
		VLOGI("RenderControl::init() ok");
	}
	return true;
}
RenderControl& RenderControl::get()
{
	if (!rct) LOG_THROW_X("RenderControl::get() null");
	return *rct;
}
vec2i RenderControl::get_size()
{
	if (!rct) return {800, 600};
	int w, h;
	SDL_GetWindowSize(rct->wnd, &w, &h);
	return {w, h};
}
