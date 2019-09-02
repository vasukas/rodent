#include <unordered_map>
#include <SDL2/SDL.h>
#include "core/settings.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "gl_utils.hpp"
#include "particles.hpp"
#include "postproc.hpp"
#include "ren_aal.hpp"
#include "ren_imm.hpp"
#include "ren_text.hpp"
#include "shader.hpp"

class RenderControl_Impl;
static RenderControl_Impl* rct;

bool RenderControl::opt_gldbg = false;
bool RenderControl::opt_fullscreen = false;



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
		std::function <void(Shader&)> reload_cb;
		bool is_crit;
	};
	std::unordered_map <std::string, ShaderInfo> shads;
	
	GLA_VertexArray* ndc_screen2_obj = nullptr;
	
	ParticleRenderer* r_part = nullptr;
	RenAAL* r_aal = nullptr;
	RenImm* r_imm = nullptr;
	RenText* r_text = nullptr;
	
	Postproc* pp_main = nullptr;
	
	std::vector< std::function<void()> > cb_resize;
	vec2i old_size;

	FullscreenValue fs_cur = FULLSCREEN_OFF;
	vec2i nonfs_size; // windowed size
	
	
	
	RenderControl_Impl(bool& ok)
	{
		if (SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			VLOGE("SDL_InitSubSystem failed - {}", SDL_GetError());
			return;
		}
		
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		
		int ctx_flags = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
		if (opt_gldbg) {
			ctx_flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
			VLOGW("Using debug OpenGL context");
		}
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, ctx_flags);
		
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
//		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
//		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
//		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
//		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		
		auto wnd_sz = AppSettings::get().wnd_size;
		nonfs_size = wnd_sz;
		if (opt_fullscreen)
		{
			fs_cur = FULLSCREEN_ENABLED;

			SDL_DisplayMode d_dm;
			SDL_GetDesktopDisplayMode(0, &d_dm);
			wnd_sz = {d_dm.w, d_dm.h};
		}
		old_size = wnd_sz;
		
		int wnd_flags = SDL_WINDOW_OPENGL;
		if (opt_fullscreen) wnd_flags |= SDL_WINDOW_FULLSCREEN;
		
		wnd = SDL_CreateWindow( "Loading...", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, wnd_sz.x, wnd_sz.y, wnd_flags );
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
				static void GLAPIENTRY f(GLenum, GLenum type, GLuint, GLenum sever, GLsizei length, const GLchar* msg, const void*) {
//					if (sever == GL_DEBUG_SEVERITY_NOTIFICATION) return;
					if (sever == GL_DEBUG_SEVERITY_HIGH) rct->crit_error = true;
					if (type == GL_DEBUG_TYPE_ERROR) VLOGE("GL: {}", std::string_view(msg, length));
					else if (opt_gldbg) VLOGV("GL [{}]: {}", type, std::string_view(msg, length));
				}
			};
			
			while (glGetError()) ;
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback( &FOO::f, nullptr );
			
			if (glGetError() == GL_NO_ERROR) VLOGI("glDebugMessageCallback set");
			else {
				glDisable(GL_DEBUG_OUTPUT);
				VLOGE("glDebugMessageCallback set failed\nOpenGL errors won't be reported");
			}
		}
		else VLOGE("glDebugMessageCallback not available\nOpenGL errors won't be reported");

		
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(0);
		
//		glEnable(GL_SCISSOR_TEST);
//		glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
		
		
		
		ndc_screen2_obj = new GLA_VertexArray;
		ndc_screen2_obj->set_buffers({ std::make_shared<GLA_Buffer>(2) });
		
		const float ps[] = {
		    -1, -1,   1, -1,   -1, 1,
		              1, -1,   -1, 1,   1, 1
		};
		ndc_screen2_obj->bufs[0]->usage = GL_STATIC_DRAW;
		ndc_screen2_obj->bufs[0]->update(12, ps);
		
		
		
		rct = this; // may be needed in these classes
		
#define INIT(VAR, NAME) \
	try {VAR = NAME::init(); VLOGI(#NAME " initialized");} \
	catch (std::exception& e) {VLOGE(#NAME " initialization failed: {}", e.what()); return;}
		
		INIT(r_text, RenText);
		INIT(r_imm, RenImm);
		INIT(r_aal, RenAAL);
		INIT(r_part, ParticleRenderer);
		
		reload_pp();
		
		ok = true;
	}
	~RenderControl_Impl()
	{
		delete pp_main;
		
		delete r_aal;
		delete r_imm;
		delete r_part;
		
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
		if (!visib) return true;
		
		auto n_size = get_size();
		if (old_size != n_size)
		{
			for (auto& c : cb_resize) if (c) c();
			old_size = n_size;
			if (fs_cur == FULLSCREEN_OFF) nonfs_size = old_size;
		}
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, n_size.x, n_size.y);
		
		if (reload_fail)
		{
			glClearColor(1, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		else
		{
			glClearColor(0.0, 0.03, 0.07, 0);
			glClear(GL_COLOR_BUFFER_BIT);
			
			cam.   set_vport_full();
			cam_ui.set_vport_full();
			
			auto frm = cam_ui.get_state();
			frm.pos = cam_ui.get_vport().size() / 2;
			cam_ui.set_state(frm);
			
			if (pp_main && use_pp) pp_main->start(passed, Postproc::CI_MAIN);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
			glBlendEquation(GL_MAX);
			
			RenImm::get().render_pre();
//			RenImm::get().render(RenImm::DEFCTX_BACK);

			RenAAL::get().render();
			glBlendEquation(GL_FUNC_ADD);
			
			if (pp_main && use_pp) pp_main->start(passed, Postproc::CI_PARTS);
			ParticleRenderer::get().render(passed);
			if (pp_main && use_pp) pp_main->finish(Postproc::CI_PARTS);
			
			RenAAL::get().render_grid(passed); // changes blending
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			
			RenImm::get().render(RenImm::DEFCTX_WORLD);
			if (pp_main && use_pp) pp_main->finish(Postproc::CI_MAIN);
			
			if (pp_main && use_pp) pp_main->render(Postproc::CI_MAIN);
			if (pp_main && use_pp) {
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
				pp_main->render(Postproc::CI_PARTS);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			
			RenImm::get().render(RenImm::DEFCTX_UI);
			RenImm::get().render_post();
		}
		
		SDL_GL_SwapWindow(wnd);
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
			VLOGW( "RenderControl::set_vsync() failed (tried to set {}) - {}", on, SDL_GetError() );
		else
			VLOGI( "RenderControl::set_vsync() ok (tried to set {})", on );
	}
	bool has_vsync()
	{
		return 0 != SDL_GL_GetSwapInterval();
	}
	void set_fscreen(FullscreenValue val)
	{
		if (val == fs_cur) return;
		
		Uint32 fs;
		switch (val)
		{
		case FULLSCREEN_OFF:     fs = 0; break;
		case FULLSCREEN_ENABLED: fs = SDL_WINDOW_FULLSCREEN; break;
		case FULLSCREEN_DESKTOP: fs = SDL_WINDOW_FULLSCREEN_DESKTOP; break;
		}

		if (SDL_SetWindowFullscreen(wnd, fs)) {
			VLOGE("SDL_SetWindowFullscreen failed - {}", SDL_GetError());
		}
		else if (val == FULLSCREEN_ENABLED)
		{
			SDL_DisplayMode d_dm;
			SDL_GetDesktopDisplayMode(0, &d_dm);

			SDL_DisplayMode dm;
			SDL_GetWindowDisplayMode(wnd, &dm);

			SDL_SetWindowSize(wnd, d_dm.w, d_dm.h); // Windows fix

			dm.w = d_dm.w; dm.h = d_dm.h;
			if (SDL_SetWindowDisplayMode(wnd, &dm))
				VLOGE("SDL_SetWindowDisplayMode failed - {}", SDL_GetError());
		}
		else if (val == FULLSCREEN_OFF)
		{
			SDL_SetWindowSize(wnd, nonfs_size.x, nonfs_size.y);
			SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		}

		fs = SDL_GetWindowFlags(wnd);
		if ((fs & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) fs_cur = FULLSCREEN_DESKTOP;
		else if ((fs & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN) fs_cur = FULLSCREEN_ENABLED;
		else fs_cur = FULLSCREEN_OFF;
	}
	FullscreenValue get_fscreen()
	{
		return fs_cur;
	}
	Shader* load_shader( const char *name, std::function <void(Shader&)> reload_cb, bool is_crit )
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
				reload_cb( *s );
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
					p.second.reload_cb( *s );
				}
			}
			else
			{
				if (s->get_obj())
					*s = Shader( 0 );
				
				if (p.second.is_crit)
					reload_fail = true;
			}
		}
		
		if (reload_fail) VLOGW("Reload failed, renderer disabled");
	}
	void reload_pp()
	{
		delete pp_main;
		try {
			pp_main = Postproc::create_main_chain();
		}
		catch (std::exception& e) {
			VLOGE("Postproc::create_main_chain() failed - {}", e.what());
			pp_main = nullptr;
		}
	}
	GLA_VertexArray& ndc_screen2()
	{
		return *ndc_screen2_obj;
	}
	RAII_Guard add_size_cb(std::function<void()> cb, bool call_now)
	{
		if (call_now) cb();
		
		size_t i=0;
		for (; i<cb_resize.size(); ++i) if (!cb_resize[i]) break;
		if (i == cb_resize.size()) cb_resize.emplace_back();
		
		cb_resize[i] = std::move(cb);
		return RAII_Guard([i](){
			if (!rct) return;
			static_cast<RenderControl_Impl*>(rct)->cb_resize[i] = {};
		});
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
	if (rct->get_fscreen() == FULLSCREEN_ENABLED)
	{
		SDL_DisplayMode dm;
		SDL_GetWindowDisplayMode(rct->wnd, &dm);
		return {dm.w, dm.h};
	}
	int w, h;
	SDL_GetWindowSize(rct->wnd, &w, &h);
	return {w, h};
}
