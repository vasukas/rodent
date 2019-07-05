/*
	Various file I\O utility classes and functions
*/

#ifndef VAS_FILE_HPP
#define VAS_FILE_HPP

#include <memory>
#include <optional>
#include <string>
#include "vas_types.hpp"

struct SDL_RWops;



/// Sets current working dir, returns false on error
bool set_current_dir( const char *dirname );

/// Checks if file is accesible
bool fexist( const char *filename );

/// Reads file as null-terminated array
std::optional<std::string> readfile( const char *filename );

/// Writes file; if size < 0, then data must be null-terminated char array
bool writefile( const char *filename, const void *data, size_t size = std::string::npos );



/// Cross-platform UTF-8 fopen
void* open_stdio_file( const char *filename, const char *mode );



/// Abstract file interface with endianess and bit support
class File
{
public:
	/// Flags and modes for open()
	enum OpenFlags
	{
		OpenRead  = 1, ///< Open only for reading
		OpenWrite = 2, ///< Open for both reading and writing
		
		/// Fail if file doesn't exist
		OpenExisting = 0,
		
		/// Create file if it doesn't exist (doesn't work with read-only)
		OpenAlways = 4,
		
		/// Even if file exists, re-create it (write is implied)
		OpenCreate = 8,

		/// Disables stdio buffering
		OpenDisableBuffer = 16
	};
	
	/// Mode for seek()
	enum SeekWhence
	{
		SeekSet, ///< Position is from the beginning of file
		SeekCur, ///< Position is offset from current position
		SeekEnd  ///< Position is from the end of file
	};
	
	/// 
	enum Endianess
	{
		EndNative,
		EndLittle, ///< 0x3412
		EndBig     ///< 0x1234
	};
	
	bool free_src = true; ///< Free source then object deleted
	Endianess def_endian = EndNative; ///< Default endianess (used by r\w*D functions)
	
	// Note: all opening and internal errors are logged
	bool error_throw = false; ///< Raise exception on any error
	bool error_flag = false; ///< Set by read\write functions to true if any I\O error occurs
	
	
	
	/// Opens file
	static File* open( const char *filename, int flags = OpenExisting | OpenRead );
	
	/// Create proxy from std::FILE*
	static File* open_std( void* src, bool free_src );

	/// Create proxy from SDL2 RWops
	static File* open_sdl( SDL_RWops* src, bool free_src );
	
	/// Create SDL2 proxy, which must be freed by user. 
	/// Note: must not be used after source is freed
	SDL_RWops* make_sdl_proxy( bool allow_free_src = false );
	
	/// Creates proxy object which will have access only to selected region. 
	/// Note: must not be used after source is freed. 
	/// Note: can't be created for unseekable files
	File* proxy_region( uint64_t from, uint64_t length, bool writeable = false );
	
	/// Closes source if 'free_src' is true
	virtual ~File() = default;
	
	
	
	// These functions do NOT set error_flag or raise exception
	// if operation is unsupported or EOF reached
	// If get_size() is available, when seek and tell must also be and vice-versa.
	
	/// Reads data, returns number of bytes read or 0 on error or EOF
	virtual size_t read( void *buf, size_t buf_size ) = 0;
	
	/// Writes data, returns number of bytes written or 0 on error
	virtual size_t write( const void *buf, size_t buf_size ) = 0;
	
	/// Returns new position or -1 if unsupported or error occurs
	virtual int64_t seek( int64_t ptr, SeekWhence whence = SeekSet ) = 0;
	
	/// Returns absolute current position (or -1 on error). 
	/// Default implementation is equivalent to seek( 0, CUR )
	virtual int64_t tell() const;
	
	/// Returns file size (or -1 on error). 
	/// Default implementation seeks to the end of file, gets position and seeks back
	virtual int64_t get_size() const;
	
	/// Flushes buffered data (returns true if not available)
	virtual bool flush() {return true;}
	
	
	
	// Endianess access functions
	// If read error occurs, sets error_flag (and may throw) and returns 0
	
	// Read/write in native endianess
	
	uint8_t r8();
	void    w8(uint8_t x);
	
	uint16_t r16N();
	uint32_t r32N();
	uint64_t r64N();
	
	void w16N(uint16_t x);
	void w32N(uint32_t x);
	void w64N(uint64_t x);
	
	// Read/write in little-endian (x86)
	
	uint16_t r16L();
	uint32_t r32L();
	uint64_t r64L();
	
	void w16L(uint16_t x);
	void w32L(uint32_t x);
	void w64L(uint64_t x);
	
	// Read/write in big-endian (network order)
	
	uint16_t r16B();
	uint32_t r32B();
	uint64_t r64B();
	
	void w16B(uint16_t x);
	void w32B(uint32_t x);
	void w64B(uint64_t x);
	
	// Read/write in def_endian
	
	uint16_t r16D();
	uint32_t r32D();
	uint64_t r64D();
	
	void w16D(uint16_t x);
	void w32D(uint32_t x);
	void w64D(uint64_t x);
	
	// Writes floating-point numbers in platform- and endianess-independent way
	
	float rpif();
	void wpif(float x);
};



/// File proxy for memory region
class MemoryFile final : public File
{
public:
	// ignores 'free_src'
	
	/// Same as mem_create(), but copies already existing data
	static MemoryFile* from_copy( const void *mem, size_t size, bool writeable = true );
	
	/// Can't be expanded, data not copied - memory NOT owned
	static MemoryFile* from_const( const void *mem, size_t size, bool writeable = false );
	
	/// Loads file to memory completely. Returns null on error
	static MemoryFile* from_file( File& file );
	
	/// Frees owned memory
	~MemoryFile();
	
	/// Allocates own memory, which can be written and expanded
	MemoryFile( size_t reserve );
	
	MemoryFile( MemoryFile&& f );
	MemoryFile( const MemoryFile& f ) = delete;
	
	size_t read( void *buf, size_t buf_size );
	size_t write( const void *buf, size_t buf_size );
	int64_t seek( int64_t ptr, SeekWhence whence = SeekSet );
	int64_t tell() const;
	int64_t get_size() const;
	
	
	
	/// Reserves additional memory (or silently fails)
	void reserve_more( size_t bytes );
	
	/// Sets size and pointer to zero, may free memory
	void reset( bool free_mem = true );
	
	/// Returns pointer
	const uint8_t *rawptr() const { return mem; }
	
	/// Directly sets memory. If ptr is null, sets only size, If cap is <0, it's same as size. 
	/// Previous owned memory is freed
	void raw_set( size_t new_size, uint8_t* raw = nullptr, ssize_t new_cap = -1 );
	
	/// For release()
	struct malloc_deleter { void operator()( void* p ); };
	
	/// Moves OWNED memory (file is zeroed). Fails (with nullptr) if const
	std::unique_ptr<uint8_t[], malloc_deleter> release( size_t& size, size_t* capacity = nullptr );
	
private:
	uint8_t *mem;
	uint64_t ptr, size, cap;
	bool a_write, a_expand;
	
	MemoryFile() = default;
	bool realloc(size_t n_cap);
};

#endif // VAS_FILE_HPP
