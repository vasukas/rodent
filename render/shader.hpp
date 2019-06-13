#ifndef SHADER_HPP
#define SHADER_HPP

#include <string>
#include "vaslib/vas_math.hpp"
#include "gl_utils.hpp"



/// Shader program wrapper
class Shader
{
public:
	/// Used by load(), should contain trailing slash
	static std::string load_path;
	
	/// Compiles shader object from source; returns 0 on fail
	static GLuint compile( GLenum type, std::string_view str );
	
	/// Links shader program; returns 0 on fail.
	/// Doesn't delete shaders
	static GLuint link( const std::vector< GLuint >& shaders );
	
	/// Creates shader program; returns null on fail.
	/// 'shaders' contain pairs of shader type and source code for it.
	/// Convenience wrapper around compile() and link() for run-time generated shaders
	static Shader* make( const char* dbg_name, const std::vector< std::pair< GLenum, std::string_view >>& shaders );
	
	/// Loads shader program from load path
	static Shader* load( const char* short_filename );
	
	
	
	std::string dbg_name;
	
	explicit Shader( GLuint prog );
	~Shader(); ///< Destroys program object
	
	void operator =( Shader&& sh );
	GLuint get_obj();
	
	bool do_validate(); ///< Performs validation check
	void bind();
	
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
//	static void set_clr(int loc, const FColor& clr);
	
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
//	void set_clr(const char *name, const FColor& clr);
	
	void set2f(int loc, vec2fp p) { set2f(loc, p.x, p.y); }
	void set2f(const char *name, vec2fp p) { set2f(name, p.x, p.y); }
	
private:
	/// Program object (can be 0, though shouldn't)
	GLuint prog;
	
	/// Set this to true to validate shader on next bind. 
	/// Check requires Verbose log level and flag is reset after
	bool validate = true;
};

#endif // SHADER_HPP
