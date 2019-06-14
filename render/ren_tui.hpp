#ifndef REN_TUI_HPP
#define REN_TUI_HPP

#include <cinttypes>

class RenTUI
{
public:
	static RenTUI& get(); ///< Returns singleton
//	virtual void set_clr(int clr, uint32_t value) = 0;
	
protected:
	friend class RenderControl_Impl;
	static RenTUI* init();
	virtual ~RenTUI();
	virtual void render() = 0;
};

#endif // REN_TUI_HPP
