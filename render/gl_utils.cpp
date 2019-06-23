#include <unordered_map>
#include "vaslib/vas_log.hpp"
#include "gl_utils.hpp"



size_t gl_type_size( GLenum t )
{
	switch( t )
	{
	case GL_FLOAT:      return 4;
	case GL_DOUBLE:     return 8;
		
	case GL_HALF_FLOAT: return 2;
	case GL_FIXED:      return 4;
		
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:
		return 1;
		
	case GL_UNSIGNED_SHORT:
	case GL_SHORT:
		return 2;
		
	case GL_UNSIGNED_INT:
	case GL_INT:
		return 4;
	}
	
	ASSERT( false, "gl_type_size() invalid enum value" );
	return 0;
}



size_t GLA_Buffer::dbg_size_now;
size_t GLA_Buffer::dbg_size_max;

GLA_Buffer::GLA_Buffer(int comp, int type, bool normalized, int usage):
    usage(usage), comp(comp), type(type), normalized(normalized)
{
	glGenBuffers( 1, &vbo );
}
GLA_Buffer::GLA_Buffer()
{
	glGenBuffers( 1, &vbo );
}
GLA_Buffer::~GLA_Buffer()
{
	glDeleteBuffers( 1, &vbo );
	dbg_size_now -= size_bytes();
}
GLA_Buffer::GLA_Buffer( GLA_Buffer&& obj )
{
	std::swap( vbo, obj.vbo );
}
void GLA_Buffer::operator =( GLA_Buffer&& obj )
{
	std::swap( vbo, obj.vbo );
}
void GLA_Buffer::swap(GLA_Buffer& obj)
{
	// params are probably same when swapping, but still
#define SW(x) std::swap(x, obj.x)
	SW(vbo);
	SW(val_count);
	SW(usage);
	SW(comp);
	SW(type);
	SW(normalized);
#undef SW
}
void GLA_Buffer::bind( GLenum target )
{
	glBindBuffer( target, vbo );
}
void GLA_Buffer::update( size_t new_val_count, const void *data )
{
	dbg_size_now -= size_bytes();
	
	bind();
	val_count = new_val_count;
	glBufferData( GL_ARRAY_BUFFER, size_bytes(), data, usage );
	
	dbg_size_now += size_bytes();
	dbg_size_max = std::max(dbg_size_max, dbg_size_now);
}
void GLA_Buffer::update_part( size_t offset, size_t val_count, const void* data )
{
	size_t raw = gl_type_size(type);
	bind();
	glBufferSubData(GL_ARRAY_BUFFER, offset * raw, val_count * raw, data);
}
void GLA_Buffer::get_part( size_t offset, size_t val_count, void* data )
{
	size_t raw = gl_type_size(type);
	bind();
	glGetBufferSubData(GL_ARRAY_BUFFER, offset * raw, val_count * raw, data);
}
size_t GLA_Buffer::size_bytes() const
{
	return gl_type_size(type) * val_count;
}



GLA_VertexArray::GLA_VertexArray()
{
	glGenVertexArrays( 1, &vao );
}
GLA_VertexArray::~GLA_VertexArray()
{
	glDeleteVertexArrays( 1, &vao );
}
GLA_VertexArray::GLA_VertexArray( GLA_VertexArray&& obj )
{
	std::swap( vao, obj.vao );
	std::swap(bufs, obj.bufs);
}
void GLA_VertexArray::operator =( GLA_VertexArray&& obj )
{
	std::swap( vao, obj.vao );
	std::swap(bufs, obj.bufs);
}
void GLA_VertexArray::swap(GLA_VertexArray& obj)
{
	std::swap(vao,  obj.vao);
	std::swap(bufs, obj.bufs);
}
void GLA_VertexArray::bind()
{
	glBindVertexArray( vao );
}
void GLA_VertexArray::set_attrib( size_t index, std::shared_ptr<GLA_Buffer> buf, size_t stride, size_t offset )
{
	if (!buf) return;
	bufs.emplace_back(std::move(buf));
	
	glBindVertexArray( vao );
	buf->bind();
	glEnableVertexAttribArray( index );
	glVertexAttribPointer( index, buf->comp, buf->type, buf->normalized, stride, reinterpret_cast< void* >(offset) );
}
void GLA_VertexArray::set_buffers( std::vector< std::shared_ptr<GLA_Buffer> > new_bufs )
{
	bufs = std::move(new_bufs);
	for (size_t i = 0; i < bufs.size(); ) {
		if (bufs[i]) {
			// actually, not neccesary an error
			for (size_t j = 0; j < i; ++j) if (bufs[j] == bufs[i])
				VLOGW("GLA_VertexArray::set_buffers() same buffers: {} and {}", i, j);
			++i;
		}
		else bufs.erase( bufs.begin() + i );
	}
	
	glBindVertexArray( vao );
	
	size_t i = 0;
	for (auto &b : bufs)
	{
		b->bind();
		glEnableVertexAttribArray( i );
		glVertexAttribPointer( i, b->comp, b->type, b->normalized, 0, nullptr );
		++i;
	}
}
void GLA_VertexArray::set_attribs( std::vector< Attrib > attrs )
{
	struct Str {size_t off = 0, str = 0, cou = 0;};
	std::unordered_map<GLA_Buffer*, Str> ss;
	ss.reserve( attrs.size() );
	
	for (auto& a : attrs)
	{
		auto b = a.buf.get();
		if (!b) LOG_THROW_X("GLA_VertexArray::set_attribs() null");
		
		auto& s = ss[b];
		s.str += a.comp * gl_type_size( b->type );
		++s.cou;
	}
	
	glBindVertexArray( vao );
	bufs.clear();
	bufs.reserve( attrs.size() );
	
	size_t i = 0;
	for (auto &a : attrs)
	{
		bufs.emplace_back(a.buf);
		auto b = a.buf.get();
		
		b->bind();
		glEnableVertexAttribArray(i);
		
		auto& s = ss[b];
		if (!s.cou) glVertexAttribPointer( i, a.comp, b->type, b->normalized, 0, nullptr );
		else {
			glVertexAttribPointer( i, a.comp, b->type, b->normalized, s.str, reinterpret_cast< void* >(s.off) );
			s.off += a.comp * gl_type_size( b->type );
		}
		++i;
	}
}



GLA_Texture::GLA_Texture()
{
	glGenTextures(1, &tex);
}
GLA_Texture::~GLA_Texture()
{
	glDeleteTextures(1, &tex);
}
GLA_Texture::GLA_Texture( GLA_Texture&& obj )
{
	std::swap(tex, obj.tex);
}
void GLA_Texture::operator =( GLA_Texture&& obj )
{
	std::swap(tex, obj.tex);
}
void GLA_Texture::swap(GLA_Texture& obj)
{
	std::swap(tex, obj.tex);
	std::swap(target, obj.target);
}
void GLA_Texture::bind(GLenum target)
{
	if (target == GL_NONE) target = this->target;
	glBindTexture(target, tex);
}
void GLA_Texture::set(GLenum internal_format, vec2i size, int level)
{
	glBindTexture(target, tex);
	glTexImage2D(target, level, internal_format, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
