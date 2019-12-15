#include <algorithm>
#include <cstdio>
#include <cerrno>

#include "vaslib/vas_log.hpp"
#include "vaslib/vas_file.hpp"

// called at first line in File::open
#define VAS_FILE_USE_OPEN_HOOK 0
#if VAS_FILE_USE_OPEN_HOOK
File* vas_file_open_hook(const char *filename, int flags);
#endif



#ifdef _WIN32

#include "wincompat.hpp"

#ifndef _MSC_VER
#include <sys/param.h>
#endif

inline uint16_t swap16(uint16_t x) {return _byteswap_ushort(x);}
inline uint32_t swap32(uint32_t x) {return _byteswap_ulong (x);}
inline uint64_t swap64(uint64_t x) {return _byteswap_uint64(x);}

#else

#include <unistd.h>

inline uint16_t swap16(uint16_t x) {return __bswap_16(x);}
inline uint32_t swap32(uint32_t x) {return __bswap_32(x);}
inline uint64_t swap64(uint64_t x) {return __bswap_64(x);}

#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define swapL_16(x) (x)
#define swapL_32(x) (x)
#define swapL_64(x) (x)
#define swapB_16(x) swap16(x)
#define swapB_32(x) swap32(x)
#define swapB_64(x) swap64(x)
#else
#define swapL_16(x) swap16(x)
#define swapL_32(x) swap32(x)
#define swapL_64(x) swap64(x)
#define swapB_16(x) (x)
#define swapB_32(x) (x)
#define swapB_64(x) (x)
#endif // __BYTE_ORDER

static uint64_t seek_ptr( uint64_t ptr, uint64_t size, int64_t val, File::SeekWhence wh )
{
#define u_val static_cast< uint64_t >(val)
	if		(wh == File::SeekSet)
	{
		if (val < 0) return 0;
		if (u_val > size) return size;
		return u_val;
	}
	else if (wh == File::SeekCur)
	{
		if (val < 0)
		{
			val = -val;
			if (u_val > ptr) return 0;
			return ptr - u_val;
		}
		if (u_val > size - ptr) return size;
		return ptr + u_val;
	}
	else if (wh == File::SeekEnd)
	{
		if (val < 0) return size;
		if (u_val > size) return 0;
		return size - u_val;
	}
#undef u_val
	return ptr;
}
static std::string errno_str( int err = errno )
{
	return FMT_FORMAT("[errno: {}] {}", err, strerror(err));
}



bool set_current_dir( const char *str )
{
#ifndef VAS_WINCOMPAT
	if (chdir( str ))
	{
		VLOGE("set_current_dir() chdir: {} (path: \"{}\")", errno_str(), str);
		return false;
	}
#else
	if (!winc_chdir( str ))
	{
		VLOGE("set_current_dir() failed (path: \"{}\")", str);
		return false;
	}
#endif
	return true;
}
bool fexist( const char *filename )
{
#ifndef VAS_WINCOMPAT
	return access(filename, F_OK) == 0;
#else
	return winc_fexist(filename);
#endif
}
std::optional<std::string> readfile( const char *filename )
{
	File* f = File::open( filename );
	if (!f) return {};
	f->error_throw = false;
	
	int an = f->get_size();
	if (an < 0) return {};
	
	std::string a;
	a.resize( an );
	int rn = f->read( a.data(), an );
	delete f;
	
	if (rn != an)
	{
		VLOGE("readfile() read failed (file: \"{}\")", filename);
		return {};
	}
	return a;
}
bool writefile( const char *filename, const void *data, size_t size )
{
	File* f = File::open( filename, File::OpenCreate );
	if (!f) return false;
	f->error_throw = false;
	
	if (size == std::string::npos) size = strlen( static_cast< const char * >(data) );
	size_t rn = f->write( data, size );
	delete f;
	
	if (rn != size)
	{
		VLOGE("writefile() write failed (file: \"{}\")", filename);
		return false;
	}
	return true;
}



void* open_stdio_file( const char *filename, const char *mode )
{
#ifndef VAS_WINCOMPAT
	return std::fopen( filename, mode );
#else
	return winc_fopen( filename, mode );
#endif
}
std::string get_file_ext(std::string_view filename)
{
	size_t i = filename.rfind('.');
	if (i == std::string::npos) return {};
	std::string s(filename.substr(i + 1));
	for (auto& c : s) if (c <= 'Z' && c >= 'A') c = (c - 'A') + 'a';
	return s;
}



int64_t File::tell() const
{
	auto t = const_cast< File* >( this );
	return t->seek( 0, SeekCur );
}
int64_t File::get_size() const
{
	int64_t ptr = tell();
	if (ptr == -1) return -1;
	auto t = const_cast< File* >( this );
	t->seek( 0, SeekEnd );
	int64_t sz = tell();
	t->seek( ptr, SeekSet );
	return sz;
}



#define FILE_RW_BASE( TYPE, NAME, BYTES )\
	TYPE File::r##NAME() \
	{\
		TYPE x;	\
		if (read( &x, BYTES ) != BYTES) \
			LOG_THROW( "File::r" #NAME "() read error or not enough data" ); \
		return x; \
	}\
	void File::w##NAME( TYPE x ) \
	{\
		if (write( &x, BYTES ) != BYTES) \
			LOG_THROW( "File::w" #NAME "() write error" ); \
	}

#define FILE_RW_END_SUB( TYPE, BITS )\
	TYPE File::r##BITS##L() { return swapL_##BITS( r##BITS##N() ); } \
	TYPE File::r##BITS##B() { return swapB_##BITS( r##BITS##N() ); } \
	TYPE File::r##BITS##D() \
	{\
		switch (def_endian) \
		{\
		case EndNative: return r##BITS##N(); \
		case EndLittle: return r##BITS##L(); \
		case EndBig:    return r##BITS##B(); \
		}\
		LOG_THROW( "File::r" #BITS "D() invalid enum" ); \
		return 0; \
	}\
	void File::w##BITS##L( TYPE x ) { return w##BITS##N( swapL_##BITS( x ) ); } \
	void File::w##BITS##B( TYPE x ) { return w##BITS##N( swapB_##BITS( x ) ); } \
	void File::w##BITS##D( TYPE x ) \
	{\
		switch (def_endian) \
		{\
		case EndNative: return w##BITS##N( x ); \
		case EndLittle: return w##BITS##L( x ); \
		case EndBig:    return w##BITS##B( x ); \
		}\
		LOG_THROW( "File::w" #BITS "D() invalid enum" ); \
	}\

#define FILE_RW_END( BITS )\
	FILE_RW_BASE( uint##BITS##_t, BITS##N, BITS/8 )\
	FILE_RW_END_SUB( uint##BITS##_t, BITS )\
	
FILE_RW_BASE( uint8_t, 8, 1 )
FILE_RW_END( 16 )
FILE_RW_END( 32 )
FILE_RW_END( 64 )

// that's IEEE754, right?

float File::rpif()
{
	uint32_t mem = r32L();
	float x;
	
	int8_t e = (mem >> 23);
	if (!e) x = 0;
	else if (e == (int8_t) 0xff) {
		int k = mem & 0x7fffff;
		if (k) x = std::numeric_limits<float>::quiet_NaN();
		else   x = std::numeric_limits<float>::infinity();
	}
	else {
		e -= 126;
		int k = mem & 0x7fffff;
		float f = k / (1 << 24) + .5f;
		x = std::ldexp(f, e);
	}
	if (mem & (1UL << 31)) x = -x;
	
	return x;
}
void File::wpif(float x)
{
	uint32_t mem = (x < 0)? (1UL << 31) : 0UL;
	if		(x == 0.f) ;
	else if (std::isinf(x)) mem |= (0x7f80 << 16);
	else if (std::isnan(x)) mem |= (0x7f80 << 16) | 1;
	else {
		int eint; float f = std::frexp(x, &eint);
		if (f < 0) f = -f;
		int8_t e = eint + 126;
		mem |= e << 23;
		int k = (f - .5f) * (1 << 24);
		mem |= k;
	}
	w32L( mem );
}



class File_STD : public File {
public:
	FILE* src;
	
	~File_STD()
	{
		if (free_src && std::fclose( src ))
			VLOGE( "File_STD:: fclose failed - {}", errno_str() );
	}
	size_t read( void *buf, size_t buf_size ) override
	{
		size_t n = std::fread( buf, 1, buf_size, src );
		if (n != buf_size && std::ferror( src ))
		{
			VLOGE( "File_STD::read() failed - {}", errno_str() );
			if (error_throw) THROW_FMTSTR( "File::read() failed" );
		}
		return n;
	}
	size_t write( const void *buf, size_t buf_size ) override
	{
		size_t n = std::fwrite( buf, 1, buf_size, src );
		if (n != buf_size)
		{
			VLOGE( "File_STD::write() failed - {}", errno_str() );
			if (error_throw) THROW_FMTSTR( "File::write() failed" );
		}
		return n;
	}
	int64_t seek( int64_t ptr, SeekWhence whence ) override
	{
		if (std::fseek( src, ptr, whence ))
		{
			VLOGD( "File_STD::seek() failed - {}", errno_str() );
			if (error_throw) THROW_FMTSTR( "File::seek() failed" );
			return -1;
		}
		return tell();
	}
	int64_t tell() const override
	{
		auto ret = std::ftell( src );
		if (ret == -1)
		{
			VLOGD( "File_STD::tell() failed - {}", errno_str() );
			if (error_throw) THROW_FMTSTR( "File::tell() failed" );
			return -1;
		}
		return ret;
	}
	bool flush() override
	{
		if (std::fflush( src ))
		{
			VLOGD( "File_STD::fflush() failed - {}", errno_str() );
			if (error_throw) THROW_FMTSTR( "File::flush() failed" );
			return false;
		}
		return true;
	}
};
std::unique_ptr<File> File::open_ptr( const char *filename, int flags )
{
	return std::unique_ptr<File> (open(filename, flags, true));
}
File* File::open( const char *filename, int flags, bool throw_on_error )
{
#if VAS_FILE_USE_OPEN_HOOK
	return vas_file_open_hook(filename, flags);
#endif
	
	int rw = flags & 0x3;
	int ex = flags & 0xc;
	
	const char *mode;
	if		(ex == OpenExisting)
	{
		if (!fexist( filename ))
		{
			VLOGE( "File::open() failed - only existing: \"{}\"", filename );
			if (throw_on_error) THROW_FMTSTR( "File::open() failed - only existing: \"{}\"", filename );
			return nullptr;
		}
		if		(rw == OpenRead)  mode = "rb";
		else if (rw == OpenWrite) mode = "ab";
		else                      mode = "r+b";
	}
	else if (ex == OpenAlways)
	{
		if		(rw == OpenRead)  mode = "rb";
		else if (rw == OpenWrite) mode = "ab";
		else                      mode = "a+b";
	}
	else if (ex == OpenCreate)
	{
		if		(rw == OpenRead)
		{
			VLOGE("File::open() failed: \"{}\"", filename);
			throw std::logic_error("File::open() invalid flags - create & read-only");
		}
		else if (rw == OpenWrite) mode = "wb";
		else                      mode = "w+b";
	}
	else
	{
		VLOGE("File::open() failed: \"{}\"", filename);
		throw std::logic_error("File::open() invalid flags");
	}
	
	FILE* f = static_cast<FILE*>( open_stdio_file(filename, mode) );
	if (!f)
	{
		VLOGE("File::open() fopen failed - {} (file: \"{}\")", errno_str(), filename);
		if (throw_on_error) THROW_FMTSTR( "File::open() failed - can't open \"{}\"", filename );
		return nullptr;
	}
	if (flags & OpenDisableBuffer)
		setbuf(f, nullptr);
	
	std::rewind( f );
	return open_std( f, true );
}
File* File::open_std( void *src_FILE, bool free_src )
{
	if (!src_FILE)
	{
		VLOGE("File::open_std() no source");
		return nullptr;
	}
	
	auto r = new File_STD;
	r->src = static_cast< FILE* >( src_FILE );
	r->free_src = free_src;
	return r;
}



class File_PROXY : public File {
public:
	File* f;
	uint64_t ptr = 0, p0, len;
	bool writeable;
	
	~File_PROXY()
	{
		if (free_src) delete f;
	}
	size_t read( void *buf, size_t buf_size ) override
	{
		int64_t sp = f->tell();
		if (sp < 0)
		{
			VLOGE( "File_PROXY::read() seek failed" );
			if (error_throw) throw std::runtime_error( "File_PROXY::read() seek failed" );
			return 0;
		}
		
		f->seek( p0 + ptr, SeekSet );
		size_t n = f->read( buf, std::min( (uint64_t) buf_size, len - ptr ) );
		ptr += n;
		
		f->seek( sp, SeekSet );
		return n;
	}
	size_t write( const void *buf, size_t buf_size ) override
	{
		if (!writeable)
		{
			VLOGE( "File_PROXY::write() not writeable" );
			if (error_throw) throw std::runtime_error( "File_PROXY::write() not writeable" );
			return 0;
		}
		
		int64_t sp = f->tell();
		if (sp < 0)
		{
			VLOGE( "File_PROXY::write() seek failed" );
			if (error_throw) throw std::runtime_error( "File_PROXY::write() seek failed" );
			return 0;
		}
		
		f->seek( p0 + ptr, SeekSet );
		size_t n = f->write( buf, std::min( (uint64_t) buf_size, len - ptr ) );
		ptr += n;
		
		f->seek( sp, SeekSet );
		return n;
	}
	int64_t seek(int64_t p, SeekWhence whence) override
	{
		ptr = seek_ptr( ptr, len, p, whence );
		return ptr;
	}
	int64_t tell() const override
	{
		return ptr;
	}
	int64_t get_size() const override
	{
		return len;
	}
};
File* File::proxy_region( uint64_t from, uint64_t length, bool writeable )
{
	if (get_size() < 0)
	{
		VLOGE( "File::proxy_region() file is unseekable" );
		return nullptr;
	}
	auto usz = static_cast< uint64_t >( get_size() );
	
	auto r = new File_PROXY;
	r->f = this;
	r->p0 = std::min( from, usz );
	r->len = std::min( length, usz );
	r->writeable = writeable;
	r->free_src = false;
	return r;
}



MemoryFile::MemoryFile( size_t res_size )
{
	mem = static_cast<uint8_t*> (malloc(res_size));
	size = 0;
	cap = res_size;
	ptr = 0;
	a_write = true;
	a_expand = true;
}
MemoryFile* MemoryFile::from_copy( const void *data, size_t size, bool writeable )
{
	uint8_t *nm = static_cast<uint8_t*> (malloc(size));
	if (!nm) {
		debugbreak();
		VLOGE("MemoryFile::from_copy() malloc failed on {} bytes", size);
		return nullptr;
	}

	MemoryFile* f = new MemoryFile;
	f->mem = nm;
	f->size = f->cap = size;
	f->ptr = 0;
	f->a_write = writeable;
	f->a_expand = true;
	
	std::memcpy( f->mem, data, size );
	return f;
}
MemoryFile* MemoryFile::from_const( const void *data, size_t size, bool writeable )
{
	MemoryFile* f = new MemoryFile;
	f->mem = static_cast<uint8_t*> (const_cast<void*> (data));
	f->size = f->cap = size;
	f->ptr = 0;
	f->a_write = writeable;
	f->a_expand = false;
	return f;
}
MemoryFile* MemoryFile::from_file( File& file )
{
	auto n = file.get_size();
	if (n < 0)
	{
		VLOGE("MemoryFile::from_file() can't get file size");
		return nullptr;
	}
	
	MemoryFile* f = new MemoryFile(n);
	if (static_cast<size_t>(n) != file.read( f->mem, n ))
	{
		VLOGE("MemoryFile::from_file() read error");
		delete f;
		return nullptr;
	}
	f->size = n;
	return f;
}
MemoryFile* MemoryFile::from_copy( const MemoryFile& file, size_t offset, size_t size, bool writeable )
{
	size = std::min(size, static_cast<size_t>(file.size) - offset);
	MemoryFile* f = new MemoryFile(size);
	std::memcpy(f->mem, file.mem + offset, size);
	f->size = size;
	f->a_write = writeable;
	return f;
}
MemoryFile::~MemoryFile()
{
	if (a_expand)
		free( mem );
}
MemoryFile::MemoryFile( MemoryFile&& f )
{
	mem = f.mem;
	size = f.size;
	cap = f.cap;
	ptr = f.ptr;
	a_write = f.a_write;
	a_expand = f.a_expand;
	
	f.mem = nullptr;
	f.reset( true );
}
size_t MemoryFile::read( void *buf, size_t buf_size )
{
	size_t n = std::min( (uint64_t) buf_size, size - ptr );
	std::memcpy( buf, mem + ptr, n );
	ptr += n;
	return n;
}
size_t MemoryFile::write( const void *buf, size_t buf_size )
{
	if (!a_write)
	{
		VLOGE( "MemoryFile::write() not writeable" );
		if (error_throw) throw std::runtime_error( "MemoryFile::write() not writeable" );
		return 0;
	}
	
	size_t left = size - ptr;
	if (left < buf_size)
	{
		if (!a_expand)
		{
			VLOGE( "MemoryFile::write() not resizable" );
			if (error_throw) throw std::runtime_error( "MemoryFile::write() not resizable" );
			return 0;
		}
		
		if (!realloc(size + buf_size))
		{
			VLOGE("MemoryFile::write() failed");
			if (error_throw) throw std::runtime_error("MemoryFile::write() realloc failed");

			buf_size = left;
		}
	}
	
	std::memcpy( mem + ptr, buf, buf_size );
	
	ptr += buf_size;
	size = std::max( size, ptr );
	return buf_size;
}
int64_t MemoryFile::seek( int64_t p, SeekWhence whence )
{
	ptr = seek_ptr( ptr, size, p, whence );
	return ptr;
}
int64_t MemoryFile::tell() const
{
	return ptr;
}
int64_t MemoryFile::get_size() const
{
	return size;
}
void MemoryFile::reserve_more( size_t bytes )
{
	if (!a_expand || !a_write) return;
	if (cap < size + bytes) {
		if (!realloc(size + bytes))
			VLOGE("MemoryFile::reserve_more() failed");
	}
}
void MemoryFile::reset( bool free_mem )
{
	ptr = size = 0;
	if (free_mem)
	{
		if (a_expand) free( mem );
		mem = nullptr;
		cap = 0;
		a_write = a_expand = true;
	}
}
void MemoryFile::raw_set( size_t new_size, uint8_t* raw, ssize_t new_cap )
{
	if (raw)
	{
		reset( true );
		
		mem = raw;
		cap = new_cap < 0 ? new_size : new_cap;
	}
	else ptr = 0;
	size = new_size;
}
void MemoryFile::malloc_deleter::operator()( void *p ){ free(p); }
std::unique_ptr <uint8_t[], MemoryFile::malloc_deleter> MemoryFile::release( size_t& ret_size, size_t* capacity )
{
	auto ret = mem;
	ret_size = size;
	if (capacity) *capacity = cap;
	
	mem = nullptr;
	reset( true );
	
	return std::unique_ptr <uint8_t[], malloc_deleter>( ret );
}
bool MemoryFile::realloc(size_t n_cap)
{
	if (uint8_t *nm = static_cast<uint8_t*> (std::realloc(mem, n_cap)))
	{
		mem = nm;
		cap = n_cap;
		return true;
	}
	debugbreak();
	VLOGE("MemoryFile::realloc() failed");
	return false;
}
