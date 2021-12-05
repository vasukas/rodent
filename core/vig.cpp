#include <condition_variable>
#include <mutex>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "render/texture.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "vaslib/vas_time.hpp"
#include "vig.hpp"

static int64_t get_ms_ticks() {return TimeSpan::since_start().ms();}

// ========================================= 
// === DEFS
// =========================================

#define KEY_LIMIT 10 ///< Maximum number of simultaneously held keys



/* Global settings */

/// Width of widget frame (pixels)
const uint vig_FrameWidth = 2;

/// Spacing between widgets (pixels)
const uint vig_WidgetSpace = 4;

/// Spacer element width and height (pixels)
const vec2i vig_SpacerWidth = {20, 10};

/// Time after which pressed key is reported again after initial event (milliseconds)
const uint vig_KeyRepeatDelay = 250;

/// Repeated key press is reported each N milliseconds
const uint vig_KeyRepeatEach = 50;

/// vig_message() duration (milliseconds)
const uint vig_MessageTime = 5000;

/// Time before tooltip is displayed on hover (milliseconds)
const uint vig_TooltipDelay = 1000;

/// Time for which tooltip is shown (milliseconds)
const uint vig_TooltipTime = 10000;

/// Width of scrollbar without frame (pixels)
const uint vig_ScrollbarWidth = 10;

/// How much normalized value is changed by mouse scroll
const float vig_SliderScroll = 0.05;



/* Rendering functions */

static void vig_draw_rect(vec2i pos, vec2i size, uint32_t clr, uint width = vig_FrameWidth) {
	RenImm::get().draw_frame(Rect::off_size(pos, size), clr, width);
}
static void vig_fill_rect(vec2i pos, vec2i size, uint32_t clr) {
	RenImm::get().draw_rect(Rect::off_size(pos, size), clr);
}
static void vig_draw_text(vec2i pos, std::string_view str, uint32_t clr = vig_CLR(Text)) {
	RenImm::get().draw_text(pos, str, clr);
}
static vec2i vig_text_size(const char *text, uint length) {
	if (text) return RenImm::get().text_size({text, length});
	return (RenText::get().mxc_size( FontIndex::Mono ) * length).int_ceil();
}
static vec2i vig_text_size(std::string_view s) {
	return vig_text_size(s.data(), s.size());
}
static int vig_text_height() {
	return std::ceil( RenText::get().line_height( FontIndex::Mono ) );
}
static void vig_draw_image(vec2i pos, vec2i size, const TextureReg& tex, uint32_t clr = 0xffffffff) {
	RenImm::get().draw_image(Rect::off_size(pos, size), tex, clr);
}



// ========================================= 
// === LOOP
// =========================================

/// Key state (see vig_on_event, vig_draw_begin and vig_draw_end on special values)
struct keydat {
	int code; ///< SDL_Scancode (0 for free slot)
	int mod;  ///< vig_Keymod_* flags
	int64_t time; ///< First pressed time or <0 to reset on same cycle press-depress
	int64_t passed; ///< Shortcut for 'cur_time - time' (checked in 'vig_is_key' only - see 'vig_begin')
};
static std::array <keydat, KEY_LIMIT> keys;

static int mouse_state = 0; ///< vig_Mouse_* flags
static vec2i mouse_pos = {-1, -1};
static vec2i mouse_press_pos = {-1, -1};
static vec2i scroll_state = {};

static std::string msg_text; ///< Message text (if empty - not shown)
static int64_t msg_time = 0; ///< Message start time (if <0 - infinite)
static int64_t msg_length = 0; ///< Message show time

static vec2i ttip_last_pos = {}; ///< Last added element rect (for tooltip)
static vec2i ttip_last_size = {};
static vec2i ttip_prev_pos = {}; ///< Previous element rect (for which tooltip was shown)
static vec2i ttip_prev_size = {};
static vec2i ttip_set_pos = {}; ///< Current tooltip element rect
static vec2i ttip_set_size = {};
static int64_t ttip_time = 0; ///< Time when tooltip was set
static std::string ttip_text; ///< Tooltip string; if empty, not displayed
static bool ttip_forced = false; ///< Is forced to be always drawn

static std::string text_input_ev; ///< Inputed UTF-8 text
static vigTextbox* textbox_sel; ///< Selected now
static vigTextbox* textbox_old; ///< Previous selected

static std::mutex msg_mutex; ///< Lock for messages and warnboxes
static std::vector<vigWarnbox> warnboxes; ///< Stack
static bool input_locked = false; ///< True if all input functions should return {}

static bool draw(vigWarnbox& box);



static std::vector<std::array<uint32_t, vig_Color_Count>> palette_stack;

void vig_push_palette() {
	auto& b = palette_stack.emplace_back();
	for (int i=0; i<vig_Color_Count; ++i) b[i] = vig_Colors[i];
}
void vig_pop_palette() {
	auto& b = palette_stack.back();
	for (int i=0; i<vig_Color_Count; ++i) vig_Colors[i] = b[i];
	palette_stack.pop_back();
}



void vig_begin() {
	int64_t time = get_ms_ticks();
	
	// set passed time for pressed keys
	for (auto &k : keys) {
		if (!k.code) continue;
		k.passed = time - k.time;
		
		// if key is repeated ...
		int rept = k.passed - vig_KeyRepeatDelay;
		if (rept > 0) {
			// check which cycle it is
			rept /= vig_KeyRepeatEach;
			// if even, set 'passed' below 'repeated' (it's checked only in vig_is_key())
			if (rept % 2) k.passed = vig_KeyRepeatDelay - 1;
		}
	}
	
	// reset mouse mask (keep only 'is_pressed')
	mouse_state &= ~(vig_Mouse_WheelUp | vig_Mouse_WheelDown | vig_Mouse_WheelLeft  | vig_Mouse_WheelRight);
	for (int i = 1; i < vig_Mouse_ButtonCount; ++i) mouse_state &= ~vig_Mouse_ClickN(i);
	
	// reset tooltip
	ttip_last_pos = ttip_last_size = {};
	ttip_text.clear();
	
	//
	text_input_ev.clear();
}
void vig_on_event(const SDL_Event& ev) {
	if		(ev.type == SDL_KEYDOWN) {
		int code = ev.key.keysym.scancode;
		
		// find if key is already detected (repeated event)
		auto it = std::find_if( keys.begin(), keys.end(), [&code](auto &&v) {return v.code == code;} );
		if (it != keys.end()) return;
		
		// find non-pressed keydat
		it = std::find_if( keys.begin(), keys.end(), [](auto &&v) {return !v.code;} );
		if (it == keys.end()) VLOGD("vig_on_event() keys limit ({})", keys.size());
		else {
			// init it
			it->code = code;
			it->time = get_ms_ticks();
			it->passed = 0;
			
			// set modifiers
			int st = ev.key.keysym.mod;
			auto& mod = it->mod;
			mod = 0;
			if (st & KMOD_CTRL)  mod |= vig_Keymod_Ctrl;
			if (st & KMOD_ALT)   mod |= vig_Keymod_Alt;
			if (st & KMOD_SHIFT) mod |= vig_Keymod_Shift;
			if (st & KMOD_CAPS)  mod |= vig_Keymod_Caps;
		}
	}
	else if (ev.type == SDL_KEYUP) {
		int code = ev.key.keysym.scancode;
		
		// find if key was pressed (i.e. if discarded it on limit)
		auto it = std::find_if( keys.begin(), keys.end(), [&code](auto &&v) {return v.code == code;} );
		if (it == keys.end()) return;
		
		if (it->passed) it->code = 0; // reset key
		else it->time = -1; // key was pressed and released on same event cycle, will be resetted in vig_draw_end()
	}
	else if (ev.type == SDL_MOUSEBUTTONDOWN) {
		// set 'pressed' flag and click
		int i = ev.button.button;
		if (i < vig_Mouse_ButtonCount) {
			if (!(mouse_state & vig_Mouse_PressN(i))) mouse_state |= vig_Mouse_ClickN(i);
			mouse_state |= vig_Mouse_PressN(i);
		}
		
		// get pos
		mouse_pos = {ev.button.x, ev.button.y};
		mouse_press_pos = mouse_pos;
	}
	else if (ev.type == SDL_MOUSEBUTTONUP) {
		// reset flag
		int i = ev.button.button;
		if (i < vig_Mouse_ButtonCount && (mouse_state & vig_Mouse_PressN(i)))
			mouse_state &= ~vig_Mouse_PressN(i);
	}
	else if (ev.type == SDL_MOUSEWHEEL) {
		int x = ev.wheel.x, y = -ev.wheel.y;
		if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {x = -x; y = -y;}
		if (x) mouse_state |= x<0? vig_Mouse_WheelLeft : vig_Mouse_WheelRight;
		if (y) mouse_state |= y<0? vig_Mouse_WheelUp : vig_Mouse_WheelDown;
	}
	else if (ev.type == SDL_MOUSEMOTION) {
		mouse_pos = {ev.motion.x, ev.motion.y};
	}
	else if (ev.type == SDL_TEXTINPUT) {
		text_input_ev += ev.text.text;
	}
}
void vig_draw_start() {
	// reset layout
	vig_lo_reset();
	
	//
	textbox_old = textbox_sel;
	textbox_sel = nullptr;
	
	//
	scroll_state = {};
	if (vig_mouse_state() & vig_Mouse_WheelLeft)  scroll_state.x = -1;
	if (vig_mouse_state() & vig_Mouse_WheelRight) scroll_state.x =  1;
	if (vig_mouse_state() & vig_Mouse_WheelUp)    scroll_state.y = -1;
	if (vig_mouse_state() & vig_Mouse_WheelDown)  scroll_state.y =  1;
}
void vig_draw_end() {
	vig_lo_reset(); // to pop scrolled zones
	vec2i root_zone_size = RenderControl::get_size();
	
	std::unique_lock msg_lock(msg_mutex);
	
	// draw message string
	if (!msg_text.empty()) {
		vec2i size = vig_element_size( msg_text );
		vec2i pos = {0, root_zone_size.y - size.y};
		
		vig_fill_rect( pos, size, vig_CLR(MsgBack) );
		vig_draw_rect( pos, size, vig_CLR(Frame), vig_FrameWidth );
		vig_draw_text( pos + vig_element_decor(), msg_text );
		
		// if not infinite, check for timeout
		if (msg_time != -1) {
			int64_t passed = get_ms_ticks() - msg_time;
			if (passed > msg_length) msg_text.clear();
		}
	}
	
	// draw warning boxes
	if (!warnboxes.empty()) {
		input_locked = false;
		if (!draw(warnboxes.back())) warnboxes.pop_back();
		input_locked = !warnboxes.empty();
	}
	
	msg_lock.unlock();
	
	// reset keys that was pressed and released on same cycle (see vig_on_event())
	for (auto &k : keys) if (k.code && k.time == -1) k.code = 0;
	
	// reset mouse clicks
	for (int i=1; i<vig_Mouse_ButtonCount; ++i) mouse_state &= ~vig_Mouse_ClickN(i);
	
	// draw tooltip
	if (!ttip_text.empty() && !input_locked) {
		// update
		if (ttip_prev_pos != ttip_set_pos || ttip_prev_size != ttip_set_size) {
			ttip_prev_pos = ttip_set_pos;
			ttip_prev_size = ttip_set_size;
			ttip_time = get_ms_ticks();
		}
		
		// draw
		int64_t passed = get_ms_ticks() - ttip_time - vig_TooltipDelay;
		if ((passed > 0 && passed < vig_TooltipTime) || ttip_forced) {
			ttip_forced = false;
			
			vec2i size = vig_element_size( ttip_text );
			vec2i pos = mouse_pos + vec2i(10, 10);
			
			// do not show any part beyond screen
			if (pos.x > root_zone_size.x - size.x) {
				pos.x = root_zone_size.x - size.x;
				if (pos.x < 0) pos.x = 0;
			}
			if (pos.y > root_zone_size.y - size.y) {
				pos.y = root_zone_size.y - size.y;
				if (pos.y < 0) pos.y = 0;
			}
			
			vig_fill_rect( pos, size, vig_CLR(TooltipBack) );
			vig_draw_rect( pos, size, vig_CLR(Frame), vig_FrameWidth );
			vig_draw_text( pos + vig_element_decor(), ttip_text );
		}
	}
	
	// lose focus
	if (textbox_old && !textbox_sel && !input_locked)
		textbox_old->on_enter(false);
}



struct MenuReg {
	std::function<void()> f;
	size_t uid;
};
static std::array<std::vector<MenuReg>, static_cast<size_t>(VigMenu::TOTAL_COUNT)> menu_fs;
static size_t menu_cur = static_cast<size_t>(VigMenu::Default);
static size_t menu_uid = 0;

void vig_draw_menues() {
	static_assert(static_cast<int>(VigMenu::TOTAL_COUNT) < 10);
	if (vig_is_key('~')) {
		for (size_t i=0; i<menu_fs.size(); ++i)
			if (vig_is_key('1' + i, true))
				menu_cur = i;
	}
	
	auto& m = menu_fs[menu_cur];
	for (size_t i=0; i<m.size(); ) {
		if (m[i].f) {m[i].f(); ++i;}
		else m.erase(m.begin() + i);
	}
}
RAII_Guard vig_reg_menu(VigMenu type, std::function<void()> draw) {
	auto i = static_cast<size_t>(type);
	menu_fs[i].push_back({ std::move(draw), ++menu_uid });
	return RAII_Guard([i, k = menu_uid]() {
		for (auto& f : menu_fs[i]) if (f.uid == k) {f.f = {}; break;}
	});
}
VigMenu vig_current_menu()
{
	return static_cast<VigMenu>(menu_cur);
}



static int char_to_scan(char c) {
	static const std::unordered_map<char, int> ks = {
	    {' ', SDL_SCANCODE_SPACE},
	    {'0', SDL_SCANCODE_0},
	    {'-', SDL_SCANCODE_MINUS},
	    {'=', SDL_SCANCODE_EQUALS},
	    {'~', SDL_SCANCODE_GRAVE},
	    {'[', SDL_SCANCODE_LEFTBRACKET},
	    {']', SDL_SCANCODE_RIGHTBRACKET},
	    {'\\', SDL_SCANCODE_BACKSLASH},
	    {'/', SDL_SCANCODE_SLASH},
	    {',', SDL_SCANCODE_COMMA},
	    {'.', SDL_SCANCODE_PERIOD},
	    {';', SDL_SCANCODE_SEMICOLON},
	    {'\'', SDL_SCANCODE_APOSTROPHE},
	    {'\t', SDL_SCANCODE_TAB},
	    {'\n', SDL_SCANCODE_RETURN},
	    {'\b', SDL_SCANCODE_BACKSPACE}
	};
	if		(c >= 'a' && c <= 'z') return SDL_SCANCODE_A + (c - 'a');
	else if (c >= '1' && c <= '9') return SDL_SCANCODE_1 + (c - '1');
	else if (auto it = ks.find(c); it != ks.end()) return it->second;
	return 0;
}
vigKeyState vig_is_key(int key, bool nomods) {
	if (input_locked) return {};
	
	// separate mods and scancode
	int mod = key & vig_Keymod_Mask, code = key & (~vig_Keymod_Mask);
	
	// get scancode
	if (code & vig_ScancodeFlag) code &= ~vig_ScancodeFlag;
	else code = char_to_scan(key);
	if (!code) return vig_Key_Not;
	
	// find key with corresponding scancode (and mods, if nomods == false)
	size_t i = 0;
	for (; i < keys.size(); ++i) if (keys[i].code == code && (nomods || keys[i].mod == mod)) break;
	
	// no such key found
	if (i == keys.size()) return vig_Key_Not;
	
	if (!keys[i].passed) return vig_Key_Just; // just pressed
	if (keys[i].passed < vig_KeyRepeatDelay) return vig_Key_Press; // not repeat yet
	return vig_Key_Repeat; // pressed for some time
}
const std::string& vig_text_input() {
	if (input_locked) {
		static std::string empty;
		return empty;
	}
	return text_input_ev;
}
int vig_mouse_state() {
	if (input_locked) return {};
	return mouse_state;
}
vec2i vig_mouse_pos() {
	if (input_locked) return {-1,-1};
	return mouse_pos;
}
vec2i vig_mouse_press_pos() {
	const int any_pressed = ((vig_Mouse_ButtonCount - 1) << vig_Mouse_ButtonCount);
	if (input_locked) return {-1,-1};
	if (!(mouse_state & any_pressed)) return mouse_pos;
	return mouse_press_pos;
}
vec2i vig_get_scroll() {
	vec2i s = scroll_state;
	scroll_state = {};
	return s;
}



void vig_message(std::string str, int len) {
	std::unique_lock msg_lock(msg_mutex);
	msg_text = std::move(str);
	msg_time = get_ms_ticks();
	msg_length = (len == vig_Message_Default)? vig_MessageTime : len;
}
void vig_tooltip(std::string_view text, bool forced) {
	vig_tooltip(text, ttip_last_pos, ttip_last_size, forced);
}
void vig_tooltip(std::string_view text, vec2i pos, vec2i size, bool forced) {
	if (text.empty() || (ttip_forced && !forced)) return;
	Rect r = Rect::off_size(pos, size);
	if (!r.contains( mouse_pos ) && !forced) return;
	ttip_set_pos = pos;
	ttip_set_size = size;
	ttip_text = text;
	ttip_forced = forced;
}



void vig_warnbox(vigWarnbox wbox) {
	std::unique_lock msg_lock(msg_mutex);
	warnboxes.emplace_back(std::move(wbox));
	input_locked = true;
}
int vig_warnbox_wait(vigWarnbox wbox) {
	if (RenderControl::get().is_rendering_thread()) {
		THROW_FMTSTR("vig_warnbox_wait() called from rendering thread! (title: {}, message: {})", wbox.title, wbox.message);
	}
	std::condition_variable cond;
	std::mutex mx;
	int ret = -1;
	
	for (size_t i=0; i<wbox.buttons.size(); ++i) {
		wbox.buttons[i].func = [&, i]{
			std::unique_lock l(mx);
			ret = i;
			cond.notify_all();
		};
	}
	vig_warnbox(std::move(wbox));
	
	std::unique_lock l(mx);
	cond.wait(l, [&]{return ret != -1;});
	return ret;
}
void vig_warnbox_index(vigWarnbox wbox, int& index) {
	index = -1;
	for (size_t i=0; i<wbox.buttons.size(); ++i)
		wbox.buttons[i].func = [p = &index, i] {*p = i;};
	vig_warnbox(std::move(wbox));
}
void vig_infobox(std::string message, bool is_error) {
	vigWarnbox box;
	box.title = is_error? "ERROR" : "INFO";
	box.message = std::move(message);
	
	auto& b = box.buttons.emplace_back();
	b.label = "OK";
	b.is_escape = b.is_default = true;
	
	vig_warnbox(std::move(box));
}
bool draw(vigWarnbox& b) {
	vec2i scr = RenderControl::get_size();
	vig_fill_rect({}, scr, 0x80);
	
	vec2i tt_sz = vig_text_size(b.title);
	vec2i ms_sz = vig_text_size(b.message);
	vec2i lb_sz = {};
	vec2i bg_bord = vec2i::one(8);
	
	std::vector<vec2i> ls;
	ls.reserve(b.buttons.size());
	for (auto& b : b.buttons) {
		ls.emplace_back( vig_element_size(b.label) );
		lb_sz.x += ls.back().x + vig_WidgetSpace;
		lb_sz.y = std::max(lb_sz.y, ls.back().y);
	}
	
	vec2i tot_sz = {
	    std::max(tt_sz.x, std::max(ms_sz.x, lb_sz.x)),
		tt_sz.y + ms_sz.y + lb_sz.y + 2*vig_text_height()
	};
	vec2i tot_off = (scr - tot_sz) /2;
	
	vig_fill_rect(tot_off - bg_bord, tot_sz + bg_bord*2, vig_CLR(MsgBack));
	vig_draw_rect(tot_off - bg_bord, tot_sz + bg_bord*2, vig_CLR(Frame));
	vig_draw_text(tot_off + vec2i((tot_sz.x - tt_sz.x) /2, 0), b.title);
	vig_draw_text(tot_off + vec2i((tot_sz.x - ms_sz.x) /2, tt_sz.y + vig_text_height()), b.message);
	
	tot_off.x += tot_sz.x - lb_sz.x;
	tot_off.y += tt_sz.y + ms_sz.y + 2*vig_text_height();
	
	if (vig_is_key(' ', true) || vig_is_key('\n', true)) {
		for (auto& b : b.buttons) {
			if (b.is_default) {
				if (b.func) b.func();
				return false;
			}
		}
	}
	if (vig_is_key(vig_ScancodeFlag | SDL_SCANCODE_ESCAPE, true)) {
		for (auto& b : b.buttons) {
			if (b.is_escape) {
				if (b.func) b.func();
				return false;
			}
		}
	}
	
	size_t i=0;
	for (auto& b : b.buttons) {
		if (vig_button(b.label, b.hotkey, false, false, tot_off, ls[i])) {
			if (b.func) b.func();
			return false;
		}
		tot_off.x += ls[i].x + vig_WidgetSpace;
		++i;
	}
	return true;
}



// ========================================= 
// === LAYOUT
// =========================================

#define SPACE int(vig_WidgetSpace)

/// Layout builder zone
struct Zone {
	vec2i pos; ///< Absolute pos
	vec2i size; ///< Current size
	vec2i max_size; ///< Maximum size (equal to size for fixed zones)
	vec2i orig_size; ///< Original zone size
	
	vec2i newpos = {SPACE, SPACE}; ///< Relative position of next element
	vec2i min_size = {}; ///< Minimal element size
	int max_h = 0; ///< Maximum element height in current line
	
	bool is_free = false;
	vec2i tl_lim = {}; // edge correction
	
	vec2i* scr_off = nullptr; // offset for scrollable zone
	
	
	
	/// Places element
	/// 'pos' - out, absolute
	/// 'size' - in, min size
	/// if not fixed, 'size' - out, max size
	bool place(vec2i& el_pos, vec2i& el_size, bool fixed) {
		// should be at least min size
		el_size = max(el_size, min_size);
		
		// check if element width bigger than zone's
		if (el_size.x > max_size.x - SPACE*2) {
			el_size.x = max_size.x - SPACE*2;
		}
		
		// expand to new row if needed
		int w_left = (max_size.x - SPACE) - newpos.x; // width left
		if (w_left < el_size.x) { // not enough for element
			newline();
			w_left = (max_size.x - SPACE) - newpos.x;
		}
		
		// set & update
		el_pos = newpos;
		newpos.x += el_size.x + SPACE;
		max_h = std::max(max_h, el_size.y);
		
		// update size for non-fixed zones
		size = max(size, el_pos + el_size);
		
		bool ret;
		if (!scr_off)
		{
			// get max size
			if (!fixed) {
				el_size.x = w_left;
				el_size.y = max_size.y - newpos.y;
			}
			
			ret = (el_pos.y < size.y); // check if element would be seen
			el_pos += pos; // absolute pos
			
			ttip_last_pos = el_pos; // set params for tooltip
			ttip_last_size = el_size;
		}
		else {
			el_pos -= *scr_off;
			
			Rect is = calc_intersection(Rect::off_size(el_pos, el_size), Rect::off_size({}, orig_size));
			ret = is.size() != vec2i{};
			
			el_pos += pos; // absolute pos
			ttip_last_pos = pos + is.off; // set params for tooltip
			ttip_last_size = is.sz;
		}
		
		if (tl_lim.x) el_pos.x = tl_lim.x - (el_pos.x + el_size.x);
		if (tl_lim.y) el_pos.y = tl_lim.y - (el_pos.y + el_size.y);
		return ret;
	}
	
	/// End current row
	void newline() {
		newpos.x = SPACE;
		newpos.y += max_h + SPACE;
		max_h = 0;
	}
};

/// Layout stack
static std::vector <Zone> lo_stack;



void vig_lo_reset()
{
	while (lo_stack.size() > 1) vig_lo_pop();
	vig_lo_toplevel(Rect::off_size({}, RenderControl::get_size()));
}
void vig_lo_push(vec2i size, bool fixed)
{
	Zone& z = lo_stack.emplace_back();
	Zone& pz = *(lo_stack.end() - 2); // parent zone
	z.size = z.max_size = z.orig_size = size;
	pz.place(z.pos, z.max_size, fixed);
}
void vig_lo_push_edge(bool is_left, bool is_upper)
{
	Zone& z = lo_stack.emplace_back();
	Zone& pz = *(lo_stack.end() - 2);
	
	if (pz.size != pz.max_size)
		throw std::logic_error("vig_lo_push_edge() fixed size parent required");
	
	z.pos = {};
	z.size = {};
	z.is_free = true;
	z.max_size = pz.max_size;
	
	if (!is_left)  z.tl_lim.x = pz.pos.x + pz.size.x;
	if (!is_upper) z.tl_lim.y = pz.pos.y + pz.size.y;
}
void vig_lo_push_scroll(vec2i outer_size, vec2i& inner_offset)
{
	Zone& z = lo_stack.emplace_back();
	Zone& pz = *(lo_stack.end() - 2); // parent zone
	z.size = z.max_size = z.orig_size = outer_size;
	pz.place(z.pos, z.size, true);
	
	RenImm::get().clip_push(Rect::off_size( z.pos, outer_size ));
	z.max_size.x = z.max_size.y = std::numeric_limits<int>::max();
	z.scr_off = &inner_offset;
}
void vig_lo_toplevel(Rect r)
{
	Zone& z = lo_stack.emplace_back();
	z.pos = r.lower();
	z.size = r.size();
	z.is_free = true;
	z.max_size = z.size;
}
void vig_lo_pop()
{
	// root element can't be popped
	if (lo_stack.size() == 1)
		throw std::logic_error("vig_lo_pop() underflow");
	
	Zone& z = lo_stack.back();
	
	// for scrollable zones
	if (z.scr_off)
	{
		const int sbwd = vig_ScrollbarWidth + vig_FrameWidth*2;
		const Rect area = Rect::off_size(z.pos, z.orig_size);
		
		// vertical
		if (z.size.y > z.orig_size.y)
		{
			vec2i pos = z.pos + vec2i(z.orig_size.x - sbwd, 0);
			vec2i size = vec2i(sbwd, z.orig_size.y);
			
			float t = float(z.scr_off->y) / z.size.y;
			float s = float(z.orig_size.y - sbwd) / z.size.y;
			if (vig_scrollbar(t, s, false, pos, size, area))
				z.scr_off->y = t * z.size.y;
		}
		// horizontal
		if (z.size.x > z.orig_size.x)
		{
			vec2i pos = z.pos + vec2i(0, z.orig_size.y - sbwd);
			vec2i size = vec2i(z.orig_size.x, sbwd);

			float t = float(z.scr_off->x) / z.size.x;
			float s = float(z.orig_size.x - sbwd) / z.size.x;
			if (vig_scrollbar(t, s, true, pos, size, area))
				z.scr_off->x = t * z.size.x;
		}
		
		z.size = z.orig_size;
		RenImm::get().clip_pop();
	}
	
	// get height and additional width and pop
	int ht = z.size.y;
	int xd = z.size.x - z.orig_size.x;
	bool was_free = z.is_free;
	lo_stack.pop_back();
	
	if (!was_free)
	{
		// apply additional width and height
		Zone& pz = lo_stack.back();
		pz.newpos.x += xd;
		pz.max_h = std::max(pz.max_h, ht);
	}
}
bool vig_lo_place(vec2i &pos, vec2i& size) {
	return lo_stack.back().place(pos, size, true);
}
void vig_lo_next() {
	lo_stack.back().newline();
}
void vig_lo_size(vec2i size) {
	lo_stack.back().min_size = size;
}
void vig_lo_cols(int count) {
	Zone& z = lo_stack.back();
	z.min_size.x = (z.max_size.x - vig_WidgetSpace * (count + 1)) / count;
}
vec2i vig_lo_get_next() {
	Zone& z = lo_stack.back();
	vec2i p = z.newpos + z.pos;
	if (z.scr_off) p -= *z.scr_off;
	return p;
}



// ========================================= 
// === HELPERS
// =========================================

vec2i vig_element_decor() {
	return {int(vig_FrameWidth) *2, int(vig_FrameWidth) *2};
}
vec2i vig_element_size(const char* text, int length) {
	vec2i size = vig_text_size(text, length);
	return size + vig_element_decor() * 2;
}
vec2i vig_element_size(std::string_view str) {
	return vig_element_size(str.data(), str.length());
}
void vig_space_tab(int width) {
	if (width < 0) width = vig_SpacerWidth.x;
	
	// don't move space to the next line
	Zone& z = lo_stack.back();
	int w_left = (z.size.x - SPACE) - z.newpos.x;
	if (w_left < width) return;
	
	vec2i p, sz = {width, 1};
	vig_lo_place(p, sz);
}
void vig_space_line(int height) {
	if (height < 0) height = vig_SpacerWidth.y;
	vec2i p, sz = {lo_stack.back().size.x - SPACE, height};
	
	vig_lo_next();
	vig_lo_place(p, sz);
	vig_lo_next();
}



// ========================================= 
// === WIDGETS
// =========================================



static fmt::memory_buffer fmtstr_buf;
#define fmtstr(Format, ...)\
	(fmtstr_buf.clear(), fmt::format_to(std::back_inserter(fmtstr_buf), FMT_STRING(Format), ##__VA_ARGS__),\
	 std::string_view(fmtstr_buf.data(), fmtstr_buf.size()))

void vig_label(std::string_view text, vec2i pos, vec2i size) {
	pos += (size - vig_text_size(text)) /2;
	vig_draw_text(pos, text);
	if (!text.empty() && text.back() == '\n') vig_lo_next();
}
void vig_label(std::string_view text) {
	// allocate space, return if invisible
	vec2i pos, size = vig_element_size(text);
	if (!vig_lo_place(pos, size)) return;
	
	// draw text
	vig_draw_text(pos + vig_element_decor(), text);
	if (!text.empty() && text.back() == '\n') vig_lo_next();
}



void vig_image(TextureReg tex, std::string_view text, vec2i pos, vec2i size) {
	vig_draw_image(pos, size, tex);
	if (!text.empty()) {
		pos.y += size.y - vig_text_size(text).y;
		vig_draw_text(pos, text);
	}
}
void vig_image(TextureReg tex, std::string_view text) {
	vec2i pos, size = tex.px_size();
	if (!vig_lo_place(pos, size)) return;
	return vig_image(tex, text, pos, size);
}



void vig_progress(std::string_view text, float t, vec2i pos, vec2i size) {
	// draw background
	vig_fill_rect(pos, size, vig_CLR(Back));
	
	int x = round(t * size.x);
	x = std::max(0, x);
	x = std::min(size.x, x);
	vig_fill_rect(pos, {x, size.y}, vig_CLR(Active));
	
	// draw frame and text
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	vig_draw_text(pos + vig_element_decor(), text);
}
void vig_progress(std::string_view text, float t) {
	vec2i pos, size = vig_element_size(text);
	if (!vig_lo_place(pos, size)) return;
	return vig_progress(text, t, pos, size);
}



bool vig_button(std::string_view text, int key, bool active, bool repeat, vec2i pos, vec2i size) {
//	if (!size.x && !size.y) size = vig_element_size( text.data(), text.length() );
	
	// check if element hovered by mouse
	bool hov = Rect::off_size(pos, size).contains(vig_mouse_press_pos());
	
	// draw background
	if (!active) vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	else		 vig_fill_rect(pos, size, hov? vig_CLR(ActiveHover) : vig_CLR(Active));
	
	// draw frame and text
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	vig_draw_text(pos + vig_element_decor(), text);
	
	// check if clicked or key pressed
	if (hov && (vig_mouse_state() & vig_Mouse_CLICK(Left))) return true;
	auto k = vig_is_key(key);
	if (k == vig_Key_Just || (k == vig_Key_Repeat && repeat)) return true;
	return false;
}
bool vig_button(std::string_view text, int key, bool active, bool repeat) {
	// allocate space, return if invisible
	vec2i pos, size = vig_element_size(text);
	if (!vig_lo_place(pos, size)) return false;
	return vig_button(text, key, active, repeat, pos, size);
}
bool vig_checkbox(bool& flag, std::string_view text, int key) {
	if (vig_button(text, key, flag)) {
		flag = !flag;
		return true;
	}
	return false;
}



bool vig_slider_t(std::string_view text, double& t, vec2i pos, vec2i size) {
//	if (!size.x && !size.y) size = vig_element_size( text.data(), text.length() );
	
	// check if element hovered by mouse
	bool hov = Rect::off_size(pos, size).contains(vig_mouse_press_pos());
	
	// draw background
	vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	
	int x = round(t * size.x);
	x = std::max(0, x);
	x = std::min(size.x, x);
	vig_fill_rect(pos, {x, size.y}, hov? vig_CLR(ActiveHover) : vig_CLR(Active));
	
	// draw frame and text
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	vig_draw_text(pos + vig_element_decor(), text);
	
	// check if pressed
	if (hov) {
		if (vig_mouse_state() & vig_Mouse_PRESS(Left)) {
			t = vig_mouse_pos().x - pos.x;
			t /= size.x;
		}
		else {
			vec2i scr = vig_get_scroll();
			if		(scr.x < 0 || scr.y > 0) t = std::max(t - vig_SliderScroll, 0.);
			else if (scr.x > 0 || scr.y < 0) t = std::min(t + vig_SliderScroll, 1.);
			else return false;
		}
		return true;
	}
	return false;
}
bool vig_slider_t(std::string_view text, double& t) {
	// allocate space, return if invisible
	vec2i pos, size = vig_element_size(text);
	if (!vig_lo_place(pos, size)) return false;
	return vig_slider_t(text, t, pos, size);
}
bool vig_slider(std::string_view text, int& value, int min, int max) {
	value = std::max(value, min);
	value = std::min(value, max);
	
	// compose string
	int ds = 0; for (int k=max; k; k /= 10) ++ds;
	auto str = fmtstr("{} [{:0{}}]", text, value, ds);
	
	// draw & process
	double t = (value - min);
	t /= (max - min);
	if (vig_slider_t(str, t)) {
		value = int_round(t * (max - min) + min);
		return true;
	}
	return false;
}
bool vig_slider(std::string_view text, double& value, double min, double max, int precision) {
	value = std::max(value, min);
	value = std::min(value, max);
	
	// compose string
	int ds = 0; for (int k=max; k; k /= 10) ++ds;
	auto str = fmtstr("{} [{:{}.{}f}]", text, value, ds + precision + 1, precision);
	
	// draw & process
	double t = (value - min);
	t /= (max - min);
	if (vig_slider_t(str, t)) {
		value = t * (max - min) + min;
		return true;
	}
	return false;
}
bool vig_scrollbar(float& offset, float span, bool is_horizontal, vec2i pos, vec2i size, Rect zone) {
	// check if element hovered by mouse
	bool press = (vig_mouse_state() & vig_Mouse_PRESS(Left)) && Rect::off_size(pos, size).contains(vig_mouse_press_pos());
	bool hov = press || Rect::off_size(pos, size).contains(vig_mouse_pos());
	bool area_hov = zone.contains(vig_mouse_pos());
	
	// draw background
	vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	
	// draw position
	if (is_horizontal) {
		int x0 = int_round(size.x * offset);
		int xn = int_round(size.x * span);
		vig_fill_rect(pos + vec2i(x0, vig_FrameWidth), vec2i(xn, vig_ScrollbarWidth),
		              hov? vig_CLR(ActiveHover) : vig_CLR(Active));
	}
	else {
		int x0 = int_round(size.y * offset);
		int xn = int_round(size.y * span);
		vig_fill_rect(pos + vec2i(vig_FrameWidth, x0), vec2i(vig_ScrollbarWidth, xn),
		              hov? vig_CLR(ActiveHover) : vig_CLR(Active));
	}
	
	// draw frame
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	
	// check if pressed
	if (press) {
		bool is_x = is_horizontal;
		offset = float(vig_mouse_pos()(is_x) - pos(is_x)) / size(is_x) - span/2;
		offset = clampf(offset, 0, 1 - span);
		return true;
	}
	if (hov || area_hov) {
		vec2i scr = vig_get_scroll();
		if		(scr.x < 0 || scr.y < 0) offset = std::max(offset - span * 0.1f, 0.f);
		else if (scr.x > 0 || scr.y > 0) offset = std::min(offset + span * 0.1f, 1.f - span);
		else return false;
		return true;
	}
	return false;
}



bool vig_selector(size_t& index, const std::vector <std::string_view> &vals) {
	index = std::min(index, vals.size() - 1);
	
	// compose string
	size_t len = 0;
	for (auto &s : vals) len = std::max(len, s.length());
	auto str = fmtstr("{:^{}}", vals[index], len);
	
	// calc subsizes
	vec2i c_size = vig_element_size(str);
	vec2i b_size = vig_element_size("<", 1);
	
	// allocate space, return if invisible
	vec2i pos, size = {c_size.x + b_size.x * 2, c_size.y};
	if (!vig_lo_place(pos, size)) return false;
	
	bool ret = false; // return value
	
	// draw increment buttons
	if (vig_button("<", 0, index != 0, true, pos, b_size)) {
		if (index) {--index; ret = true;}
	}
	if (vig_button(">", 0, index != vals.size() - 1, true, {pos.x + c_size.x + b_size.x, pos.y}, b_size)) {
		if (index != vals.size() - 1) {++index; ret = true;}
	}
	
	// draw selector button
	pos.x += b_size.x;
	size.x -= b_size.x * 2;
	
	// check if element hovered by mouse
	bool hov = Rect::off_size(pos, size).contains(vig_mouse_pos());
	
	// draw background
	vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	
	// draw frame and text
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	vig_draw_text(pos + vig_element_decor(), str);
	
	// check if pressed
	if (hov) {
		if (vig_mouse_state() & vig_Mouse_CLICK(Left)) {
			if (index != vals.size() - 1) {++index; ret = true;}
		}
		if (vig_mouse_state() & vig_Mouse_CLICK(Right)) {
			if (index) {--index; ret = true;}
		}
	}
	
	return ret;
}
bool vig_num_selector(size_t& index, size_t num) {
	index = std::min(index, num - 1);
	
	// compose string
	int ds = 0; for (size_t k=num; k; k /= 10) ++ds;
	auto str = fmtstr("{:{}}", index, ds);
	
	// calc subsizes
	vec2i c_size = vig_element_size(str);
	vec2i b_size = vig_element_size("<", 1);
	
	// allocate space, return if invisible
	vec2i pos, size = {c_size.x + b_size.x * 2, c_size.y};
	if (!vig_lo_place(pos, size)) return false;
	
	bool ret = false; // return value
	
	// draw increment buttons
	if (vig_button("<", 0, index != 0, true, pos, b_size)) {
		if (index) {--index; ret = true;}
	}
	if (vig_button(">", 0, index != num - 1, true, {pos.x + c_size.x + b_size.x, pos.y}, b_size)) {
		if (index != num - 1) {++index; ret = true;}
	}
	
	// draw selector button
	pos.x += b_size.x;
	size.x -= b_size.x * 2;
	
	// check if element hovered by mouse
	bool hov = Rect::off_size(pos, size).contains(vig_mouse_pos());
	
	// draw background
	vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	
	// draw frame and text
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	vig_draw_text(pos + vig_element_decor(), str);
	
	// check if pressed
	if (hov) {
		if (vig_mouse_state() & vig_Mouse_CLICK(Left)) {
			if (index != num - 1) {++index; ret = true;}
		}
		if (vig_mouse_state() & vig_Mouse_CLICK(Right)) {
			if (index) {--index; ret = true;}
		}
	}
	
	return ret;
}



vigAverage::vigAverage(float seconds, float default_passed)
{
	reset(seconds, default_passed);
}
vigAverage::~vigAverage() = default;
void vigAverage::reset(float seconds, float default_passed)
{
	time_default = default_passed;
	vals.clear();
	vals.resize(seconds / default_passed);
	upd_tex = true;
}
void vigAverage::add(float v, std::optional<float> time_opt)
{
	int twas = static_cast<int>(tcou);
	tcou += time_opt? *time_opt : time_default;
	bool f = (twas != static_cast<int>(tcou));
	
	vptr = (vptr + 1) % vals.size();
	vals[vptr] = {(v > 0? v : 0), f};
	upd_tex = true;
}
void vigAverage::draw()
{
	const int tex_k = 1; // texture width multiplier
	const int tex_alpha = 0xa0; // texture transparency
	
	float avg = 0,
	      min = std::numeric_limits<float>::max(),
	      max = std::numeric_limits<float>::lowest();
	for (auto& v : vals) {
		avg += v.first;
		min = std::min(min, v.first);
		max = std::max(max, v.first);
	}
	avg /= vals.size();
	
	auto s = FMT_FORMAT("Avg {:6.3f} Now {:6.3f}\nMin {:6.3f} Max {:6.3f}", avg, vals[vptr].first, min, max);
	vec2i size = vig_element_size(s);
	size.x += 2 + tex_k * vals.size();
	
	vec2i pos;
	if (!vig_lo_place(pos, size)) return;
	vig_draw_text(pos + vig_element_decor(), s);
	
	if (upd_tex) {
		upd_tex = false;
		if (!tex) {
			px.reset( new uint8_t [vals.size() * size.y * 3] );
			tex.reset( Texture::create_empty(vec2i(vals.size(), size.y), Texture::FMT_RGB, Texture::FIL_NEAREST) );
		}
		
		if (tex_range < max) tex_range = max * 1.1;
		else if (tex_range > max * 3) tex_range /= 2;
		
		const uint8_t clrs[] = {
		    0xb0, 0xb0, 0xb0,
		    0xff, 0xff, 0xff,
		    0x40, 0xff, 0x40
		};
		
		int pitch = tex->get_size().x * 3;
		for (size_t i=0; i<vals.size(); ++i) {
			int n = tex->get_size().y * vals[i].first / tex_range;
			n = clamp(n, 0, tex->get_size().y);
			int clr = (i == vptr) ? 2 : vals[i].second;
			
			for (int k=0; k<n; ++k)
				for (int j=0; j<3; ++j) px[(tex->get_size().y-1-k)*pitch + i*3 + j] = clrs[clr*3 + j];
			for (int k=n; k<tex->get_size().y; ++k)
				for (int j=0; j<3; ++j) px[(tex->get_size().y-1-k)*pitch + i*3 + j] = 0;
		}
		
		tex->update_full(px.get());
	}
	
	pos.x += (size.x - tex_k * vals.size());
	vec2i sz;
	sz.x = tex_k * vals.size();
	sz.y = size.y;
	vig_draw_image(pos, sz, tex.get(), 0xffffff00 | tex_alpha);
}



vigTextbox::~vigTextbox() {
	if (textbox_sel == this) textbox_sel = nullptr;
	if (textbox_old == this) textbox_old = nullptr;
}
void vigTextbox::draw()
{
	vec2i pos, size = vig_element_size(nullptr, max_chars);
	if (!vig_lo_place(pos, size)) return;
	
	const int off = vig_element_decor().x;
	const int ftw = RenText::get().width_mode(FontIndex::Mono);
	
	// check selection
	bool hov = Rect::off_size(pos, size).contains(vig_mouse_pos());
	if (vig_mouse_state() & (vig_Mouse_CLICK(Left) | vig_Mouse_CLICK(Right))) {
		if (hov) textbox_sel = this;
		else if (textbox_old == this) on_enter(false);		
		
		// set pointer
		ptr = (vig_mouse_pos().x - pos.x - off + ftw/2) / ftw;
	}
	else if (textbox_old == this) textbox_sel = textbox_old;
	bool sel = (textbox_sel == this);
	
	// process input
	if (sel) {
		ptr = std::min(ptr, max_chars);
		
		auto add = [this](const char *us) {
			auto s = tmpstr_8to32(us);
			for (auto& c : s) {
				if (str.length() == max_chars) break;
				if (c == '\n') continue; // just in case
				if (allow && !allow(c)) continue;
				str.insert(ptr, 1, c);
				++ptr;
			}
		};
		
		if		(vig_is_key(vig_ScancodeFlag | SDL_SCANCODE_LEFT)) {
			if (ptr) --ptr;
		}
		else if (vig_is_key(vig_ScancodeFlag | SDL_SCANCODE_RIGHT)) {
			if (ptr != max_chars && ptr != str.length()) ++ptr;
		}
		else if (vig_is_key(vig_ScancodeFlag | SDL_SCANCODE_LEFT | vig_Keymod_Ctrl)) {
			ptr = str.rfind(' ', ptr);
			if (ptr == std::string::npos) ptr = 0;
		}
		else if (vig_is_key(vig_ScancodeFlag | SDL_SCANCODE_RIGHT | vig_Keymod_Ctrl)) {
			ptr = str.find(' ', ptr);
			if (ptr == std::string::npos) ptr = str.length();
		}
		else if (vig_is_key('\n')) {
			on_enter(true);
		}
		else if (vig_is_key('\b')) {
			if (ptr) {
				str.erase(ptr, 1);
				--ptr;
			}
		}
		else if (vig_is_key('\b' | vig_Keymod_Ctrl)) {
			size_t i = str.rfind(' ', ptr);
			if (i == std::string::npos) i = 0;
			str.erase(i, ptr - i);
			ptr = i;
		}
		else if (vig_is_key('c' | vig_Keymod_Ctrl)) {
			SDL_SetClipboardText( tmpstr_32to8(str).data() );
		}
		else if (vig_is_key('x' | vig_Keymod_Ctrl)) {
			SDL_SetClipboardText( tmpstr_32to8(str).data() );
			str.clear();
		}
		else if (vig_is_key('v' | vig_Keymod_Ctrl)) {
			if (SDL_HasClipboardText()) {
				auto s = SDL_GetClipboardText();
				if (s) add(s);
				SDL_free(s);
			}
		}
		else if (!vig_text_input().empty()) {
			add(vig_text_input().c_str());
		}
	}
	
	// draw background & frame
	if (sel) vig_fill_rect(pos, size, hov? vig_CLR(ActiveHover) : vig_CLR(Active));
	else if (is_invalid) vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Incorrect));
	else vig_fill_rect(pos, size, hov? vig_CLR(BackHover) : vig_CLR(Back));
	vig_draw_rect(pos, size, vig_CLR(Frame), vig_FrameWidth);
	
	// draw text
	TextRenderInfo tri;
	tri.str = str.data();
	tri.length = str.length();
	RenImm::get().draw_text(pos + vig_element_decor(), tri, vig_CLR(Text));
	
	// draw pointer
	if (sel) {
		ptr = std::min(ptr, max_chars);
		vig_fill_rect(pos + vec2i(vig_element_decor().x + ptr * ftw, 0), {static_cast<int>(vig_FrameWidth), size.y}, -1);
	}
}
void vigTextbox::on_enter(bool Enter_or_focuslost) {
	if (on_fin) on_fin(*this, Enter_or_focuslost);
	if (textbox_sel == this) textbox_sel = nullptr;
}
void vigTextbox::allow_uint() {
	allow = [](char32_t c) {
		return c <= '9' && c >= '0';
	};
}
void vigTextbox::allow_ipaddr() {
	allow = [](char32_t c) {
		return (c <= '9' && c >= '0') ||
		       (c <= 'f' && c >= 'a') ||
		       (c <= 'F' && c >= 'A') ||
		       (c == '.' || c == ':');
	};
}
void vigTextbox::allow_ascii() {
	allow = [](char32_t c) {
		return 0 == (c & 0x80);
	};
}
void vigTextbox::allow_name(bool unicode) {
	allow = [unicode](char32_t c) {
		return (c <= '9' && c >= '0') ||
		       (c <= 'z' && c >= 'a') ||
		       (c <= 'Z' && c >= 'A') ||
		       (c == '_') ||
		       (unicode && 0 != (c & 0x80));
	};
}
void vigTextbox::set_u8(std::string s) {
	str = string_8to32(s);
	ptr = std::min(ptr, str.length());
}
std::string vigTextbox::get_u8() const {
	return string_32to8(str);
}



void vigTableLC::calc()
{
	const int space = use_space ? SPACE : 0;
	
	std::vector<int> cmax;
	cmax.resize(size.x);
	
	maxsz = {space, 0};
	int y_cur = space;
	
	for (int y=0; y<size.y; ++y)
	{
		int my = 0;
		
		for (int x=0; x<size.x; ++x)
		{
			auto& e = els[y * size.x + x];
			if (e.str) e.size = vig_element_size(*e.str);
			
			cmax[x] = std::max(cmax[x], e.size.x + space);
			my = std::max(my, e.size.y);
			e.pos.y = y_cur;
		}
		
		maxsz.y = std::max(maxsz.y, y_cur + my + space);
		y_cur += my + space;
		
		for (int x=0; x<size.x; ++x)
		{
			auto& e = els[y * size.x + x];
			e.max_size.y = my;
		}
	}
	
	for (int x=0; x<size.x; ++x)
	{
		for (int y=0; y<size.y; ++y)
		{
			auto& e = els[y * size.x + x];
			e.pos.x = maxsz.x + space;
			e.max_size.x = cmax[x];
		}
		maxsz.x += cmax[x];
	}
}
vec2i vigTableLC::place()
{
	vec2i p, sz = maxsz;
	vig_lo_place(p, sz);
	return p;
}
void vigTableLC::set_size(vec2i new_size)
{
	auto old = std::move(els);
	els.resize( new_size.area() );
	
	for (int y=0; y < std::min(size.y, new_size.y); ++y)
	for (int x=0; x < std::min(size.x, new_size.x); ++x)
	{
		els[y * new_size.x + x] = std::move(els[y * size.x + x]);
	}
	
	size = new_size;
}
vigTableLC::Element& vigTableLC::get(vec2i pos)
{
	if (is_in_bounds(pos, size))
		return els[pos.y * size.x + pos.x];
	
	throw std::runtime_error("vigTableLC::get() out of bounds");
}
