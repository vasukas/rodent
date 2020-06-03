#ifndef REN_CTL_HPP
#define REN_CTL_HPP

#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class  Camera;
struct GLA_VertexArray;
struct ImageInfo;
union  SDL_Event;
struct SDL_Window;



class RenderControl
{
public:
	enum FullscreenValue
	{
		FULLSCREEN_OFF,
		FULLSCREEN_ENABLED,
		FULLSCREEN_DESKTOP
	};
	
	static bool opt_gldbg; // init options
	
	/// If set, writes screen data (RGBA, y-flipped) to it before swapping buffers. 
	/// Reset after each render() call
	ImageInfo* img_screenshot = nullptr;
	
	
	
	static bool init(); ///< Initialize singleton (including all other rendering singletons)
	static RenderControl& get(); ///< Returns singleton
	virtual ~RenderControl() = default; ///< Also destroys all related singletons
	
	
	
	static vec2i get_size(); ///< Returns window width & height in pixels
	virtual int get_max_tex() = 0; ///< Returns maximum texture dimension possible
	
	virtual Camera& get_world_camera() = 0;
	virtual Camera& get_ui_camera()    = 0; ///< Always reset before rendering
	
	virtual bool is_visible() = 0; ///< Returns true if window is visible on screen
	virtual SDL_Window* get_wnd() = 0;
	
	virtual TimeSpan get_passed() = 0; ///< Returns last 'passed' value which was passed to render()

	
	
	/// Returns current system coordinates, may be slow
	virtual vec2i get_current_cursor() = 0;
	
	/// Render everything to screen. 
	/// Returns false if some unrecoverable error occured
	virtual bool render( TimeSpan passed ) = 0;
	
	/// Performs rendering-related processing
	virtual void on_event( SDL_Event& ev ) = 0;
	
	
	
	/// Sets vertical synchronization
	virtual void set_vsync( bool on ) = 0;
	
	/// Returns true if vsync is enabled
	virtual bool has_vsync() = 0;
	
	/// Sets fullscreen option value
	virtual void set_fscreen(FullscreenValue val) = 0;
	
	/// Returns real fullscreen option value
	virtual FullscreenValue get_fscreen() = 0;
	
	
	
	/// Reloads all shaders
	virtual void reload_shaders() = 0;
	
	/// Returns internal VAO representing full screen in NDC as two triangles - 6 vertices of vec2 (CCW winding)
	virtual GLA_VertexArray& ndc_screen2() = 0;
	
	/// Adds callback to be called at screen size change. Returns callback deleter
	[[nodiscard]] virtual RAII_Guard add_size_cb(std::function<void()> cb, bool call_now = true) = 0;
	
	/// Executes function on render thread. 
	/// Throws if RenderControl no longer exists
	virtual void exec_task(callable_ref<void()> f) = 0;
	
	/// Returns true if current thread is main one
	virtual bool is_rendering_thread() const = 0;
};

#endif // REN_CTL_HPP
