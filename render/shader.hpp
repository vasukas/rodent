#ifndef SHADER_HPP
#define SHADER_HPP

#include <string>
#include "vaslib/vas_math.hpp"
#include "gl_utils.hpp"

/// Shader program wrapper
class Shader
{
public:
	struct Define
	{
		std::string name;
		std::string value; ///< Call rebuild() to apply changes
		bool is_default = true; ///< Set to false if value shouldn't be replaced with default on reload
	};
	
	struct Callbacks
	{
		std::function<void(Shader&)> post_build = {}; ///< Called on rebuild, shader is already built and bound
		std::function<void(Shader&)> pre_build = {}; ///< Called on rebuild before compiling and linking shader
		std::function<void(Shader&)> pre_link = {}; ///< Called on rebuild between attaching shaders and linking
	};
	
	Callbacks cbs;
	bool is_critical;
	
	
	
	static ptr_range<Shader*> get_all_ptrs(); ///< Returns all shaders
	
	/// Never returns null
	static std::unique_ptr<Shader> load(const char *name, Callbacks cbs, bool is_critical = false, bool do_build = true);
	
	Shader(const Shader&) = delete;
	~Shader();
	
	const std::string& get_name() const {return prog_name;}
	bool is_ok() const {return prog || never_built;} ///< Returns true if can be used
	
	Define* get_def(std::string_view name); ///< Returns define from 'def_list' or null if not found
	void set_def(std::string_view name, std::string value); ///< Sets define value if it exists. Shader marked for rebuild
	GLuint get_prog() {return prog;} ///< May return 0
	
	bool reload(); ///< Loads shader sources and rebuilds shader if it was built before
	bool rebuild(bool forced = true); ///< Just rebuilds program
	
	void bind(); ///< Builds shader if not already
	
	void set1i(const char *name, int v);
	void set1f(const char *name, float v);
	void set2f(const char *name, float a, float b);
	void set3f(const char *name, float x, float y, float z);
	void set4f(const char *name, float r, float g, float b, float a);
	void setfv(const char *name, const float *v, int n);
	void set2mx(const char *name, const float *v, bool do_transpose = false);
	void set3mx(const char *name, const float *v, bool do_transpose = false);
	void set4mx(const char *name, const float *v, bool do_transpose = false);
	void set_rgba(const char *name, uint32_t clr, float mul = 1.f);
	void set_clr(const char *name, const FColor& clr);
	void set2f(const char *name, const vec2fp& p);
	
	
	
private:
	GLuint prog = 0; ///< Program object (can be 0)
	std::vector<size_t> src_ixs; ///< Index into sh_col
	std::vector<Define> def_list; ///< All defines from separate shaders
	
	std::string prog_name; ///< Same as filename
	bool validate = false;
	
	std::vector<uint32_t> no_such_loc; // hash
	bool never_built = true;
	
	
	
	Shader() = default;
	void reset_prog();
	
	/// Returns binding point or -1 if not found
	GLint find_loc( const char *uniform_name );
	
	static void set1i(int loc, int v);
	static void set1f(int loc, float v);
	static void set2f(int loc, float a, float b);
	static void set3f(int loc, float x, float y, float z);
	static void set4f(int loc, float r, float g, float b, float a);
	static void setfv(int loc, const float *v, int n);
	static void set2mx(int loc, const float *v, bool do_transpose = false);
	static void set3mx(int loc, const float *v, bool do_transpose = false);
	static void set4mx(int loc, const float *v, bool do_transpose = false);
	static void set_rgba(int loc, uint32_t clr, float mul = 1.f);
	static void set_clr(int loc, const FColor& clr);
	static void set2f(int loc, const vec2fp& p);
};

#endif // SHADER_HPP
