#include <condition_variable>
#include <mutex>
#include <thread>
#include <SDL2/SDL.h>
#include "core/settings.hpp"
#include "utils/res_image.hpp"
#include "vaslib/vas_log.hpp"
#include "camera.hpp"
#include "control.hpp"
#include "gl_utils.hpp"
#include "postproc.hpp"
#include "ren_text.hpp"
#include "shader.hpp"

class RenderControl_Impl;
static RenderControl_Impl* rct;

bool RenderControl::opt_gldbg = false;



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
static const char *dbgo_type_name(GLenum x)
{
	switch (x)
	{
	case GL_DEBUG_TYPE_ERROR: return "Error";
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "Deprecated";
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UB";
	case GL_DEBUG_TYPE_PORTABILITY: return "Portability";
	case GL_DEBUG_TYPE_PERFORMANCE: return "Performance";
	default: return "Unknown";
	}
}
static const char *dbgo_sever_name(GLenum x)
{
	switch (x)
	{
	case GL_DEBUG_SEVERITY_HIGH:   return "High";
	case GL_DEBUG_SEVERITY_MEDIUM: return "Medium";
	case GL_DEBUG_SEVERITY_LOW:    return "Low";
	case GL_DEBUG_SEVERITY_NOTIFICATION: return "Info";
	default: return "Unknown";
	}
}



class RenderControl_Impl : public RenderControl
{
public:
	bool crit_error = false;
	
	std::thread::id mainthr_id;
	SDL_Window* wnd = nullptr;
	SDL_GLContext glctx = nullptr;
	bool visib = true;
	
	int max_tex_size = 1024;
	Camera cam, cam_ui;
	
	GLA_VertexArray* ndc_screen2_obj = nullptr;
	RenText* r_text = nullptr;
	Postproc* pp_main = nullptr;
	
	std::vector< std::function<void()> > cb_resize;
	vec2i old_size;

	FullscreenValue fs_cur = FULLSCREEN_OFF;
	vec2i nonfs_size; // windowed size
	
	TimeSpan last_passed = TimeSpan::fps(30);
	
	
	
	RenderControl_Impl(bool& ok)
	{
		mainthr_id = std::this_thread::get_id();
		
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
			VLOGW("RenderControl:: Using debug OpenGL context");
		}
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, ctx_flags);
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		
		auto wnd_sz = AppSettings::get().wnd_size;
		auto opt_fs = AppSettings::get().fscreen;
		nonfs_size = wnd_sz;
		if (opt_fs == AppSettings::FS_Fullscreen)
		{
			fs_cur = FULLSCREEN_ENABLED;

			SDL_DisplayMode d_dm;
			SDL_GetDesktopDisplayMode(0, &d_dm);
			wnd_sz = {d_dm.w, d_dm.h};
		}
		old_size = wnd_sz;
		
		int wnd_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
		if (opt_fs == AppSettings::FS_Fullscreen) wnd_flags |= SDL_WINDOW_FULLSCREEN;
		else if (opt_fs == AppSettings::FS_Borderless) wnd_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		else if (opt_fs == AppSettings::FS_Maximized) wnd_flags |= SDL_WINDOW_MAXIMIZED;
		
		wnd = SDL_CreateWindow( "Loading...", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, wnd_sz.x, wnd_sz.y, wnd_flags );
		if (!wnd)
		{
			VLOGE("SDL_CreateWindow failed - {}", SDL_GetError());
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			return;
		}

		wnd_sz = get_size();
		VLOGD("RenderControl:: resized to {} {}", wnd_sz.x, wnd_sz.y);
		
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
//					if (sever == GL_DEBUG_SEVERITY_HIGH) rct->crit_error = true;
					if (type == GL_DEBUG_TYPE_ERROR) VLOGE("GL [Error/{}]: {}", dbgo_sever_name(sever), std::string_view(msg, length));
					else if (opt_gldbg) VLOGV("GL [{}/{}]: {}", dbgo_type_name(type), dbgo_sever_name(sever), std::string_view(msg, length));
					
//					if (type == GL_DEBUG_TYPE_ERROR && sever == GL_DEBUG_SEVERITY_HIGH)
//						debugbreak();
				}
			};
			
			while (glGetError()) ;
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback( &FOO::f, nullptr );
			
			if (glGetError() == GL_NO_ERROR) VLOGI("glDebugMessageCallback set");
			else {
				glDisable(GL_DEBUG_OUTPUT);
				VLOGE("glDebugMessageCallback set failed - OpenGL errors won't be reported");
			}
		}
		else VLOGE("glDebugMessageCallback not available - OpenGL errors won't be reported");

		
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		
		
		
		ndc_screen2_obj = new GLA_VertexArray;
		ndc_screen2_obj->set_buffers({ std::make_shared<GLA_Buffer>(2) });
		
		const float ps[] = {
		    -1, -1,   1, -1,   1,  1,
		     1,  1,  -1,  1,  -1, -1
		};
		ndc_screen2_obj->bufs[0]->usage = GL_STATIC_DRAW;
		ndc_screen2_obj->bufs[0]->update(12, ps);
		
		
		
		rct = this; // may be needed in these classes
		
		try {
			r_text = RenText::init();
			pp_main = Postproc::init();
		}
		catch (std::exception& e)
		{
			VLOGE("RenderControl:: init exception: {}", e.what());
			return;
		}
		
		ok = true;
	}
	~RenderControl_Impl()
	{
		delete pp_main;
		delete r_text;
		delete ndc_screen2_obj;
	
		SDL_GL_DeleteContext( glctx );
		SDL_DestroyWindow( wnd );
		if (wnd) SDL_QuitSubSystem( SDL_INIT_VIDEO );
		
		tasks_interrupted = true;
		task_cv.notify_all();
		
		rct = nullptr;
	}
	
	int     get_max_tex()      { return max_tex_size; }
	Camera& get_world_camera() { return cam; }
	Camera& get_ui_camera()    { return cam_ui; }
	bool    is_visible()       { return visib; }
	SDL_Window* get_wnd()      { return wnd; }
	TimeSpan    get_passed()   { return last_passed; }
	
	vec2i get_current_cursor()
	{
		vec2i m, w;
		SDL_GetGlobalMouseState(&m.x, &m.y);
		SDL_GetWindowPosition(wnd, &w.x, &w.y);
		return m - w;
	}
	
	bool render( TimeSpan passed )
	{
		proc_tasks();
		
		last_passed = passed;
		if (crit_error) return false;
		if (!visib) return true;
		
		auto n_size = get_size();
		if (old_size != n_size)
		{
			for (auto& c : cb_resize) if (c) c();
			old_size = n_size;
			if (fs_cur == FULLSCREEN_OFF) nonfs_size = old_size;
			VLOGD("RenderControl:: resized to {} {}", n_size.x, n_size.y);
			
			// blank screen for one frame
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, n_size.x, n_size.y);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			pp_main->render_reset();
			return true;
		}
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, n_size.x, n_size.y);
		
		bool shaders_ok = true;
		for (auto& s : Shader::get_all_ptrs()) {
			if (s->is_critical && !s->is_ok()) {
				shaders_ok = false;
				break;
			}
		}
		
		if (!shaders_ok)
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
			
			try {
				pp_main->render();
			}
			catch (std::exception& e) {
				VLOGE("RenderControl::render() exception: {}", e.what());
				return false;
			}
			
			if (img_screenshot)
			{
				vec2i sz = get_size();
				img_screenshot->reset(sz, ImageInfo::FMT_RGBA);
				glFinish();
				glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
				glReadPixels(0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, img_screenshot->raw());
				img_screenshot = nullptr;
			}
		}
		
		SDL_GL_SwapWindow(wnd);
		return true;
	}
	void on_event( SDL_Event& ev )
	{
		if (ev.type == SDL_WINDOWEVENT)
		{
//			if (ev.window.windowID != SDL_GetWindowID(wnd)) return;
			
			if		(ev.window.event == SDL_WINDOWEVENT_HIDDEN ||
			         ev.window.event == SDL_WINDOWEVENT_MINIMIZED ) visib = false;
			else if (ev.window.event == SDL_WINDOWEVENT_SHOWN ||
			         ev.window.event == SDL_WINDOWEVENT_EXPOSED ) visib = true;
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
	void reload_shaders()
	{
		VLOGW("RenderControl::reload_shaders() called");
		for (auto& s : Shader::get_all_ptrs())
			s->reload();
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
	
	
	
	std::vector<std::pair<bool, callable_ref<void()>*>> tasks;
	std::condition_variable task_cv;
	std::mutex task_m;
	bool tasks_interrupted = false;
	
	void exec_task(callable_ref<void()> f)
	{
		if (mainthr_id == std::this_thread::get_id()) {
			f();
			return;
		}
		size_t i=0;
		{
			std::unique_lock lock(task_m);
			for (; i < tasks.size(); ++i) if (!tasks[i].second) break;
			if (i == tasks.size()) tasks.emplace_back();
			tasks[i] = std::make_pair(false, &f);
		}
		
		std::unique_lock lock(task_m);
		task_cv.wait(lock, [&]
		{
			if (tasks_interrupted) return true;
			if (!tasks[i].first) return false;
			
			tasks[i].second = nullptr;
			for (auto& t : tasks) if (t.second) return true;
			tasks.clear();
			return true;
		});
		if (tasks_interrupted)
			throw std::runtime_error("RenderControl::exec_task() interrupted");
	}
	void proc_tasks()
	{
		std::unique_lock lock(task_m);
		for (auto& t : tasks)
		{
			if (t.second && !t.first) {
				(*t.second)();
				t.first = true;
			}
		}
		task_cv.notify_all();
	}
};
bool RenderControl::init()
{
	if (!rct)
	{
		bool ok = false;
		new RenderControl_Impl (ok);
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
