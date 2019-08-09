#ifndef REN_CTL_HPP
#define REN_CTL_HPP

#include <functional>
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class  Camera;
struct GLA_VertexArray;
union  SDL_Event;
struct SDL_Window;
class  Shader;



class RenderControl
{
public:
	enum FullscreenValue
	{
		FULLSCREEN_OFF,
		FULLSCREEN_ENABLED,
		FULLSCREEN_DESKTOP
	};
	
	bool shader_fail = false; ///< If true, fails if load_shader fails
	bool use_pp = true; ///< Is post-processing used
	
	bool draw_tui = false; ///< Hack for DbgMenu
	
	static bool opt_gldbg;
	static bool opt_fullscreen;
	
	
	
	static bool init(); ///< Initialize singleton (including all other rendering singletons)
	static RenderControl& get(); ///< Returns singleton
	virtual ~RenderControl() = default; ///< Also destroys all related singletons
	
	
	
	static vec2i get_size(); ///< Returns window width & height in pixels
	virtual int get_max_tex() = 0; ///< Returns maximum texture dimension possible
	
	virtual Camera* get_world_camera() = 0;
	virtual Camera* get_ui_camera()    = 0; ///< Always reset before rendering
	
	virtual bool    is_visible()       = 0; ///< Returns true if window is visible on screen
	virtual SDL_Window* get_wnd() = 0;
	
	
	
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
	
	
	
	/// Loads shader and stores it internally. Never returns null. 
	/// Reload callback is called every time when shader is loaded, including first one. 
	/// Shader is already bound before calling callback. 
	/// If is_crit is false, shader treated as optional and doesn't cause internal renderer errors
	virtual Shader* load_shader( const char *name, std::function <void(Shader&)> reload_cb = {}, bool is_crit = true ) = 0;
	
	/// Reloads and recompiles all shaders
	virtual void reload_shaders() = 0;
	
	/// Reloads post-processing chain
	virtual void reload_pp() = 0;
	
	/// Returns internal VAO representing full screen in NDC as two triangles - 6 vertices of vec2
	virtual GLA_VertexArray& ndc_screen2() = 0;
	
	/// Adds callback to be called at screen size change. Returns callback deleter
	[[nodiscard]] virtual RAII_Guard add_size_cb(std::function<void()> cb) = 0;
};

#endif // REN_CTL_HPP
