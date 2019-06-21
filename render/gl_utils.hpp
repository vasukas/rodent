#ifndef GL_UTILS_HPP
#define GL_UTILS_HPP

#include <memory>
#include <vector>
#include <GL/glew.h>
#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"



/// Returns byte size for GL types enumerations (GL_FLOAT etc)
size_t gl_type_size( GLenum type_enum );

/// Converts normalized float
inline int8_t norm_i8(float x) {
	if (x < 0) return x > -1.f ? x * 128 : -128;
	return x < 1.f ? x * 127 : 127;
}



/// Wrapper for vertex buffer object
struct GLA_Buffer
{
	static size_t dbg_size_now; ///< info: total size of all buffer objects in bytes
	static size_t dbg_size_max; ///< info: max size of buffers ever was, in bytes
	
	GLuint vbo = 0;
	int val_count = 0; ///< Number of values in buffer (info only)
	int usage = GL_DYNAMIC_DRAW;
	
	int comp = 2; ///< Number of components (only as info for VAO)
	int type = GL_FLOAT; ///< Value type
	bool normalized = false; ///< (only as info for VAO)
	
	
	GLA_Buffer(int comp, int type = GL_FLOAT, bool normalized = false, int usage = GL_DYNAMIC_DRAW);
	GLA_Buffer(); ///< Creates object
	~GLA_Buffer(); ///< Destroys object
	
	GLA_Buffer( const GLA_Buffer& ) = delete;
	void operator =( const GLA_Buffer& ) = delete;
	
	GLA_Buffer( GLA_Buffer&& );
	void operator =( GLA_Buffer&& );
	
	void swap(GLA_Buffer& obj);
	
	
	void bind( GLenum target = GL_ARRAY_BUFFER );
	void update( size_t new_val_count, const void *data = nullptr ); ///< Binds buffer
	void update_part( size_t offset, size_t val_count, const void* data );
	void get_part( size_t offset, size_t val_count, void* data );
	size_t size_bytes() const;
};



/// Wrapper for vertex array object
struct GLA_VertexArray
{
	GLuint vao = 0;
	std::vector< std::shared_ptr<GLA_Buffer> > bufs; ///< May contain same buffers
	
	
	GLA_VertexArray(); ///< Creates object
	~GLA_VertexArray(); ///< Destroys object
	
	GLA_VertexArray( const GLA_VertexArray& ) = delete;
	void operator =( const GLA_VertexArray& ) = delete;
	
	GLA_VertexArray( GLA_VertexArray&& );
	void operator =( GLA_VertexArray&& );
	
	void swap(GLA_VertexArray& obj);
	
	
	/// Binds array object
	void bind();
	
	/// Sets attribute; binds both VAO and VBO
	void set_attrib( size_t location, std::shared_ptr<GLA_Buffer> buf, size_t stride = 0, size_t offset = 0 );
	
	/// Note: buffers MUST be different, stride and offsets are NOT calculated!
	void set_buffers( std::vector< std::shared_ptr<GLA_Buffer> > bufs );
	
	struct Attrib
	{
		std::shared_ptr<GLA_Buffer> buf;
		int comp; ///< number of components
	};
	/// Automatically sets all attributes with calculated strides and offsets; binds both VAO and VBOs
	void set_attribs( std::vector< Attrib> attrs );
};



struct GLA_Texture
{
	GLuint tex = 0;
	GLenum target = GL_TEXTURE_2D; ///< default
	
	
	GLA_Texture(); ///< Creates object
	~GLA_Texture(); ///< Destroys object
	
	GLA_Texture( const GLA_Texture& ) = delete;
	void operator =( const GLA_Texture& ) = delete;
	
	GLA_Texture( GLA_Texture&& );
	void operator =( GLA_Texture&& );
	
	void swap(GLA_Texture& obj);
	
	
	/// Binds to specified target or to default one, if GL_NONE
	void bind(GLenum target = GL_NONE);
	
	/// Allocates storage for 2D; binds texture. 
	/// Sets filtering to linear and enables clamping
	void set(GLenum internal_format, vec2i size, int level = 0);
	
	operator GLuint() {return tex;}
};



struct GLA_Framebuffer
{
	GLuint fbo = 0;
	vec2i size = {};
	
	GLA_Framebuffer() {
		glGenFramebuffers(1, &fbo);
	}
	~GLA_Framebuffer() {
		glDeleteFramebuffers(1, &fbo);
	}
	
	GLA_Framebuffer( const GLA_Framebuffer& ) = delete;
	void operator =( const GLA_Framebuffer& ) = delete;
	
	GLA_Framebuffer( GLA_Framebuffer&& obj ) {swap(obj);}
	void operator =( GLA_Framebuffer&& obj ) {swap(obj);}
	
	void swap(GLA_Framebuffer& obj) {
		std::swap(fbo, obj.fbo);
		std::swap(size, obj.size);
	}
	
	void bind() {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}
	void attach_tex(GLenum point, GLuint tex) {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, tex, 0);
	}
};

#endif // GL_UTILS_HPP
