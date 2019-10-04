#ifndef PP_GRAPH_HPP
#define PP_GRAPH_HPP

#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_types.hpp"
#include "control.hpp"
#include "shader.hpp"

class PP_Filter;



class PP_Node
{
public:
	const std::string name;
	const bool has_input; ///< Is target (disabled if have no inputs)
	const bool has_output; ///< Is provider (disabled if have no target)
	
	
	PP_Node(std::string name, bool has_input = true, bool has_output = true); ///< Adds self to PP_Graph
	virtual ~PP_Node(); ///< Removes self from graph
	
	/// Called once before each step. Returns true if node is enabled
	virtual bool prepare() = 0;
	
	/// Called once per step when all inputs are ready (only if enabled)
	virtual void proc(GLuint output_fbo) = 0;
	
	/// Returns input framebuffer (called once per input, but only before proc())
	virtual GLuint get_input_fbo() = 0;
};



class PP_Graph
{
public:
	static PP_Graph& get(); ///< Returns singleton
	
	/// Output order - lower drawn earlier amongst providers for same target
	virtual void connect(std::string output, std::string input, int order = 0) = 0;
	
protected:
	friend class RenderControl_Impl;
	static PP_Graph* init();
	virtual ~PP_Graph();
	virtual void render() = 0;
	
	friend PP_Node;
	virtual void add_node(PP_Node* node) = 0;
	virtual void del_node(PP_Node* node) = 0;
};



/// Chain of fullscreen filters
class PPN_Chain : public PP_Node
{
public:
	std::function<bool()> is_enabled;
	
	// pre_draw is called before rendering to output buffer
	PPN_Chain(std::string name, std::vector<std::unique_ptr<PP_Filter>> fts_in, std::function<void()> pre_draw_in);
	
private:
	std::vector<std::unique_ptr<PP_Filter>> fts;
	std::function<void()> pre_draw;
	RAII_Guard rsz_g;
	
	GLA_Framebuffer fbo_s[2];
	GLA_Texture tex_s[2];
	
	bool clear_fbo = false;
	bool bs_index = false; ///< Index of FBO to which next draw will be
	bool bs_last; ///< Is last effect
	
	GLuint fbo_out;
	
	
	bool prepare() override;
	void proc(GLuint output_fbo) override;
	GLuint get_input_fbo() override;
	
	friend PP_Filter;
	void draw(bool is_last);
};



/// Wrapper for renderers
class PPN_InputDraw : public PP_Node
{
public:
	bool enabled;
	PPN_InputDraw(std::string name, std::function<void(GLuint fbo)> func, bool enabled = true)
	    : PP_Node(name, false, true), enabled(enabled), func(std::move(func))
	{}
	
private:
	std::function<void(GLuint)> func;
	
	bool prepare() override {return enabled;}
	void proc(GLuint fbo) override {func(fbo);}
	GLuint get_input_fbo() override {throw std::logic_error("PPN_InputDraw::get_input_fbo() called");}
};



/// Wrapper for display FBO
class PPN_OutputScreen : public PP_Node
{
public:
	PPN_OutputScreen() : PP_Node("display", true, false) {}
	
private:
	bool prepare() override {return true;}
	void proc(GLuint) override {}
	GLuint get_input_fbo() override {return 0;}
};



/// Fullscreen shader effect
class PP_Filter
{
public:
	bool enabled = true;
	Shader* sh = nullptr;
	
	
	virtual ~PP_Filter() = default;
	virtual void proc() = 0;
	
	/// Is internal state ok
	bool is_ok() {return sh && sh->is_ok() && is_ok_int();}
	virtual bool is_ok_int() {return true;}
	
protected:
	void draw(bool is_last) {_chain->draw(is_last);}
	TimeSpan get_passed() {return RenderControl::get().get_passed();}
	
private:
	friend PPN_Chain;
	PPN_Chain* _chain = nullptr;
};

#endif // PP_GRAPH_HPP
