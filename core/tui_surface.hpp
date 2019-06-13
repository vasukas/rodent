#ifndef TUI_SURFACE_HPP
#define TUI_SURFACE_HPP

#include <vector>
#include "vaslib/vas_math.hpp"

enum : char32_t {
	SCH_BULLET = 0x2022,
	SCH_CHECKER_LIGHT = 0x2591,
	SCH_CHECKER_MED   = 0x2592,
	SCH_CHECKER_HEAVY = 0x2593,
	SCH_BLOCK_FULL = 0x2588,
};

enum : int {
	// color flags
	TUI_BLACK = 0,
	TUI_RED   = 1,
	TUI_GREEN = 2,
	TUI_BLUE  = 4,
	TUI_WHITE = 7,
	
	// preset colors
	TUI_SET_TEXT = 16,
	TUI_SET_BACK
};

struct TUI_Char
{
	char32_t sym = 0;
	int fore = TUI_SET_TEXT;
	int back = TUI_SET_BACK;
	
	static TUI_Char none() {return {static_cast<char32_t>(-1), -1, -1};} ///< All values set to -1	
};

char32_t boxdraw_char(bool left, bool right, bool up, bool down);



class TUI_Surface
{
public:
	vec2i size = {};
	std::vector<TUI_Char> cs;
	bool upd_any;
	
	
	TUI_Surface() = default;
	TUI_Surface(vec2i size, TUI_Char with = {});
	
	void clear(TUI_Char with = {});
	void resize_clear(vec2i new_size, TUI_Char with = {});
	void resize_keep(vec2i new_size);
	
	void set_from(vec2i off, TUI_Surface* src, vec2i src_pos, vec2i size);
	void set_from(vec2i off, TUI_Surface* src); ///< Draws whole surface
	
	bool in_bounds(vec2i pos) const; ///< Checks if position is in bounds
	size_t pti(vec2i pos) const; ///< Just converts position to index
	
	const TUI_Char& get(vec2i pos) const; ///< Bounds-safe
	void set(vec2i pos, TUI_Char ch); ///< Bounds-safe, sets update
	void change(vec2i pos, TUI_Char ch); ///< Bounds-safe, changes only values which != -1
	
	void set_rect(Rect r, TUI_Char ch); ///< Filled rectangle
	void change_rect(Rect r, TUI_Char ch); ///< Changes only values which are >= 0
};



/// Draws lines and boxes using box drawing symbols
struct TUI_BoxdrawHelper
{
	TUI_Surface* sur = nullptr;
	
	void init(TUI_Surface& s) {sur = &s; clear();}
	void box(Rect r);
	void hline(int x1, int x2, int y);
	void vline(int y1, int y2, int x);
	void set(vec2i p);
	void submit();
	void clear();
	
private:
	std::vector<bool> flags;
};

#endif // TUI_SURFACE_HPP
