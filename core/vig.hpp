// Small unoptimized immediate-mode GUI library

#ifndef VIG_HPP
#define VIG_HPP

#include <functional>
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_math.hpp"

class  Texture;
struct TextureReg;
union  SDL_Event;



/* Main loop */

/// Must be called at the start of new loop step
void vig_begin();

/// Must be called only between calls to vig_begin() and vig_draw_start()
void vig_on_event(const SDL_Event& ev);

/// Must be called after vig_begin() when all events are processed. 
/// GUI elements can be rendered only between calls to this and vig_draw_end()
void vig_draw_start();

/// Draws registered menus
void vig_draw_menues();

/// Must be called after vig_draw_start() when all GUI rendering is complete
void vig_draw_end();



/* Control */

/// Selection is made with TILDA + [1-9]
enum class VigMenu
{
	Default,
	DebugRenderer,
	DebugGame,
	
	TOTAL_COUNT ///< Do not use
};

/// Registers new menu
[[nodiscard]] RAII_Guard vig_reg_menu(VigMenu type, std::function<void()> draw);

///
VigMenu vig_current_menu();



/* Keyboard events */

#define vig_ScancodeFlag 0x0800
//
#define vig_Keymod_Ctrl  0x1000
#define vig_Keymod_Alt   0x2000
#define vig_Keymod_Shift 0x4000
#define vig_Keymod_Caps  0x8000 ///< Caps Lock was hold
//
#define vig_Keymod_Mask  (~0xfff) ///< SDL_Scancode mask

enum vigKeyState {
	vig_Key_Not = 0,    ///< Not pressed
	vig_Key_Press  = 1, ///< Pressed (when not Repeat; can be used as flag)
	vig_Key_Just   = 3, ///< Just was pressed
	vig_Key_Repeat = 5  ///< Repeated key press
};

/// Returns key state ('key' is combination of vig_Keymod_* flags and ASCII character or SDL_Scancode with vig_ScancodeFlag)
vigKeyState vig_is_key(int key, bool ignore_modifiers = false);

/// Returns UTF-8 text input
const std::string& vig_text_input();



/* Mouse events */

/// Default mouse button indices (equal to ones from SDL2)
enum vigMouseButton {
	vig_Mouse_Left = 1,
	vig_Mouse_Middle,
	vig_Mouse_Right,
	
	vig_Mouse_ButtonCount = 8 ///< Maximum number of buttons
};
#define vig_Mouse_CLICK(NAME)   vig_Mouse_ClickN(vig_Mouse_ ## NAME)
#define vig_Mouse_PRESS(NAME)	vig_Mouse_PressN(vig_Mouse_ ## NAME)

// Reported on wheel events. Vertical are in screen direction.
#define vig_Mouse_WheelUp    0x1
#define vig_Mouse_WheelDown  0x2
#define vig_Mouse_WheelLeft  0x4
#define vig_Mouse_WheelRight 0x8

/// Button was clicked (reported on button depress)
#define vig_Mouse_ClickN(index) (0x10 << (index - 1))
/// Button is pressed
#define vig_Mouse_PressN(index) ((0x10 << vig_Mouse_ButtonCount) << (index - 1))

/// Returns combined vig_Mouse_* flags
int vig_mouse_state();

/// Returns absolute mouse position
vec2i vig_mouse_pos();

/// Returns scroll state - but only once per render step!
vec2i vig_get_scroll();



/* Messages */
// Note: message and warnbox funcs are thread-safe

enum vigMessageTime {
	vig_Message_Default = -2, ///< default time (vig_MessageTime)
	vig_Message_Infinite      ///< no timeout
};

/// Displays message at the bottom of the screen. 
/// If string is empty, clears current message
void vig_message(std::string str, int show_length_ms = vig_Message_Default);



/// Button for warnbox
struct vigWarnboxButton {
	std::string label; ///< Single line
	std::function <void(void)> func; ///< Function called when button is pressed
	int hotkey = 0;
	bool is_default = false; ///< Pressed on space or enter
	bool is_escape = false; ///< Pressed on escape
};

/// Warning box displayed on top of UI
struct vigWarnbox {
	std::string title; ///< Single line, caps recommended
	std::string message;
	std::vector<vigWarnboxButton> buttons;
};

/// Pushes new on top of the stack
void vig_warnbox(vigWarnbox wbox);

/// Creates and pushes warnbox with single OK button
void vig_infobox(std::string message, bool is_error = true);



/// Sets tooltip for last added element
void vig_tooltip(std::string_view text);

/// Sets tooltip for area (internal)
void vig_tooltip(std::string_view text, vec2i pos, vec2i size);



/* Auto-layout */

/*
	Layout is organized as zone\element stack.
	Zones can contain other zones and elements, but don't process input; vice-versa with elements.
	
	Zone can have fixed or dynamic size. In second case, minimal size must be defined;
	maximum size is limited only by size of parent zone.
	
	Additional top-level zone can be specified - it's stack is independent of default,
	and not involved in normal layout calculations, so it may overlap with other stacks.
	
	- All functions operate on the top element.
	- Elements are placed in rows from left to right, from top to bottom.
	- To place non-zone element, use 'vig_lo_place' function.
*/

/// Clear all stacks and place single fullscreen top-level zone
void vig_lo_reset();

/// Push new zone
void vig_lo_push(vec2i size, bool fixed);

/// Push dynamic sized zone attached to parent edge. Uses reversed placement direction
void vig_lo_push_edge(bool Left_or_right, bool Top_or_bottom);

/// Scrollable zone with fixed outer size and unlimited inner. 
/// Note: inner offset must be valid until call to pop()
void vig_lo_push_scroll(vec2i outer_size, vec2i& inner_offset);

/// Push top-level zone (absolute coordinates)
void vig_lo_toplevel(Rect r);

/// Pop zone (or remove top-level)
void vig_lo_pop();

/// Allocates space for element. 
/// 'pos' is returned absolute position of element. 
/// Returns false if element is completely out of zone borders (but still initializes everything)
bool vig_lo_place(vec2i& pos, vec2i& size);

/// Ends current row of elements
void vig_lo_next();

/// Set minimal element size
void vig_lo_size(vec2i size);

/// Sets element x-size from zone size and number of columns
void vig_lo_cols(int count);

/// Returns absolute position of next element
vec2i vig_lo_get_next();



/* Widget helpers */

/// Returns half size of element decoration
vec2i vig_element_decor();

/// Returns full size of element containing only text. 
/// Params are same as in 'vig_text_size'
vec2i vig_element_size(const char* text, int length);

/// Returns full size of element containing only text
vec2i vig_element_size(std::string_view str);

/// Adds empty space in current row (-1 for default width)
void vig_space_tab(int width = -1);

/// Ends current line and adds empty line (-1 for default height)
void vig_space_line(int height = -1);



/* Labels */

// Note: all can be multiline; if last symbol is '\n', calls lo_next()

/// Centered text
void vig_label(std::string_view text, vec2i pos, vec2i size);

/// Just text display
void vig_label(std::string_view text);

/// Formatted text display
#define vig_label_a(Format, ...) vig_label(fmt::format(FMT_STRING(Format), ##__VA_ARGS__))



/* Display widgets */

/// Image with label at the bottom left corner 
void vig_image(TextureReg tex, std::string_view text, vec2i pos, vec2i size);

/// Image with label at the bottom left corner 
void vig_image(TextureReg tex, std::string_view text = {});

/// Shows [0-1] progress with label
void vig_progress(std::string_view text, float t, vec2i pos, vec2i size);

/// Shows [0-1] progress with label
void vig_progress(std::string_view text, float t);



/* Buttons */

/// Simple button with text. 
/// Returns true if clicked, false otherwise. 
/// If 'active' is true, then vig_Color_Active used instead of vig_Color_Back
bool vig_button(std::string_view text, int key, bool active, bool repeat, vec2i pos, vec2i size);

/// Simple button with text. 
/// Returns true if clicked, false otherwise. 
/// If 'active' is true, then vig_Color_Active used instead of vig_Color_Back
bool vig_button(std::string_view text, int key = 0, bool active = false, bool repeat = false);

/// Simple checkbox with text (wrapper around button). 
/// Returns true if value changed
bool vig_checkbox(bool& flag, std::string_view text, int key = 0);



/* Sliders */

/// 't' is normalized value [0, 1]. 
/// Returns true if value changed, otherwise t is unchanged
bool vig_slider_t(std::string_view text, double& t, vec2i pos, vec2i size);

/// 't' is normalized value [0, 1]. 
/// Returns true if value changed, otherwise t is unchanged
bool vig_slider_t(std::string_view text, double& t);

/// Integer slider. 
/// Returns true if value changed
bool vig_slider(std::string_view text, int& value, int min = 0, int max = 100);

/// Floating-point slider. 
/// Returns true if value changed
bool vig_slider(std::string_view text, double& value, double min = 0., double max = 1., int precision = 3);

///
bool vig_scrollbar(float& offset, float span, bool is_horizontal, vec2i pos, vec2i size, Rect zone = {});



/* Selectors */

/// Enumeration selector (combined widget). 
/// Returns true if value changed
bool vig_selector(size_t& index, const std::vector<std::string> &vals, int key_minus = 0, int key_plus = 0);

/// Numerical selector (combined widget). 
/// Returns true if value changed
bool vig_num_selector(size_t& index, size_t num, int key_minus = 0, int key_plus = 0);



/* Stateful widgets */


/// Displays average absolute value with min/max and graphic
struct vigAverage
{
	vigAverage(float seconds = 3, float default_passed = 1. / 30);
	~vigAverage();
	
	void reset(float seconds, float default_passed);
	void add(float v, std::optional<float> seconds_passed = {});
	void draw();
	
private:
	float time_default;
	std::vector<std::pair<float, bool>> vals;
	size_t vptr = 0;
	float tcou = 0;
	
	std::unique_ptr<Texture> tex;
	std::unique_ptr<uint8_t[]> px;
	bool upd_tex = false;
	float tex_range = 1;
};


/// Single-line without selection
struct vigTextbox
{
	std::u32string str; ///< UTF-8 by default
	size_t max_chars = 16; ///< Max characters allowed
	bool is_invalid = false; ///< If true, draws special background
	
	std::function<bool(char32_t)> allow; ///< If set, returns true if character can be added
	std::function<void(vigTextbox&, bool)> on_fin; ///< If set, called on focus lost (false) or enter (true)
	
	vigTextbox() = default;
	~vigTextbox();
	void draw();
	void on_enter(bool Enter_or_focuslost);
	
	void allow_uint(); ///< Sets 'allow' to numerics only
	void allow_ipaddr(); ///< Sets 'allow' to IPv4/IPv6 address symbols
	void allow_ascii(); ///< Sets 'allow' to ASCII chars
	void allow_name(bool unicode); ///< Sets 'allow' to alphanumerics and underscores
	
private:
	size_t ptr = 0;
};


/// Table layout calculator
struct vigTableLC
{
	struct Element
	{
		std::optional<std::string> str; ///< Input: if set, size calculated from it
		vec2i size = {}; ///< Input: element size
		
		vec2i pos = {}; ///< Output: position relative to origin
		vec2i max_size = {}; ///< Output: max element size
	};
	
	bool use_space = true; ///< If true, widget spaces are applied
	
	vigTableLC() = default;
	vigTableLC(vec2i size) {set_size(size);}
	
	/// Calculates element positions from sizes. 
	/// If place is true, places total size in current zone. 
	/// Returns total size
	vec2i calc(bool place = true);
	
	void set_size(vec2i new_size);
	vec2i get_size() const {return size;}
	
	Element& get(vec2i pos); ///< Throws on error
	std::vector<Element>& get_all() {return els;}
	
private:
	std::vector<Element> els;
	vec2i size = {};
};

#endif // VIG_HPP
