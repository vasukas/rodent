#ifndef TUI_LAYER_HPP
#define TUI_LAYER_HPP

union SDL_Event;
#include <string>
#include "tui_surface.hpp"

class TUI_Layer
{
public:
	struct Field
	{
		bool is_transp = false; ///< Is SET_BACK or TRANSP used for background
		
		void clear();
		void set(TUI_Char* s, size_t s_len); ///< Processes newlines
		void set(std::string_view str, size_t highlight = std::string::npos);
		void set(int n) {set(std::to_string(n));}
		void set_bar(float t, bool show_percent = false); ///< Fills available space with progress bar
		
		Field() {sur = nullptr;}
		Field(TUI_Surface& sur, Rect r, bool is_transp): is_transp(is_transp), sur(&sur), r(r) {clear();}
		Field(const Field& f): is_transp(f.is_transp), sur(f.sur), r(f.r) {}
		
//		std::string prefix; ///< Added before value
		
	private:
		TUI_Surface* sur;
		Rect r;
	};
	
	TUI_Surface sur;
	bool transparent = true; ///< If false, lower layers not drawn
	
	
	
	static vec2i screen_size(); ///< Returns screen size in chars
	static TUI_Layer* get_stack_top(); ///< May return null
	
	static float char_sz_mul; ///< Character size multiplier. Must be set before initing renderer
	static bool render_all(TUI_Surface& dst); ///< Returns false if nothing changed
	
	TUI_Layer(); ///< Initializes fullscreen transparent surface
	virtual ~TUI_Layer(); ///< Removes layer from stack
	
	void bring_to_top(); ///< Places layer on top of stack
	void hide(); ///< Removes layer from stack
	
	virtual void on_event(const SDL_Event& ev) = 0;
	virtual void render() = 0; ///< Draws to 'sur'
	
	[[nodiscard]] Field mk_field(Rect r, int is_transp = -1);
};

#endif // TUI_LAYER_HPP
