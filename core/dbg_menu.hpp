#ifndef DBG_MENU_HPP
#define DBG_MENU_HPP

#include "vaslib/vas_cpp_utils.hpp"
struct TimeSpan;

enum DbgMenuGroup
{
	DBGMEN_ETC = 1,
	DBGMEN_RENDER,
};

#define dbgm_label   DbgMenu::get().label
#define dbgm_button  DbgMenu::get().button
#define dbgm_check   DbgMenu::get().checkbox

/// Immediate-mode UI for debugging utilities
class DbgMenu
{
public:
	struct Section
	{
		std::function<void()> proc;
		std::string name = {};
		DbgMenuGroup group = DBGMEN_ETC;
		char hotkey = 0; // only a-z
	};
	
	static DbgMenu& get(); ///< Returns singleton
	virtual ~DbgMenu();
	
	/// Registers new section
	virtual RAII_Guard reg(Section sec) = 0;
	
	/// Draw text
	virtual void label(std::string_view s) = 0;
	
	/// Draw button, returns true if it was pressed
	virtual bool button(std::string_view s, char hotkey) = 0;
	
	/// Draw checkbox button, returns true if state changed
	bool checkbox(bool& flag, std::string_view s, char hotkey);
	
	virtual void render(TimeSpan passed, bool has_input) = 0;
	virtual void on_key(int scan) = 0;
};

#endif // DBG_MENU_HPP
