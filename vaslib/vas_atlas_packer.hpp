#ifndef VAS_ATLAS_PACKER_HPP
#define VAS_ATLAS_PACKER_HPP

#include <cinttypes>
#include <memory>
#include <vector>



/// Makes packed texture atlas from sprites (scanline algorithm)
class AtlasPacker
{
public:
	/// Input sprite info
	struct InputInfo
	{
		uint64_t id; ///< unique id
		int w = 0, h = 0; ///< width and height
		int spacing = -1; ///< spacing size override (if -1, uses same as atlas at the moment)
		
		bool operator <( const InputInfo& im );
	};
	
	/// Output sprite info
	struct SpriteData : InputInfo
	{
		size_t index; ///< atlas index
		int x, y; ///< atlas coordinates
	};
	
	/// Output atlas info
	struct AtlasInfo
	{
		int w, h; ///< width and height
		std::vector <SpriteData> sprs; ///< sprites in this atlas
	};
	
	int min_size = 64; ///< minimal size increase step, must be at least 1 (for texture swizzling and as optimization)
	int max_size = 1024; ///< maximum texture dimensions
	int bpp = 4; ///< bytes per pixel

	int space_size = 0; ///< spacing size (default)
	bool space_borders = true; ///< add spacing on borders
	
	
	
	/// Empty atlas
	AtlasPacker();
	
	/// Resets internal state
	void reset();

	/// Adds sprite. Throws if id already used
	void add( const InputInfo& info );
	
	/// Returns sprite position in texture after building or nullptr if it doesn't exist
	SpriteData* get( uint64_t id );
	
	/// Make atlases after all sprites are added. Throws on error
	std::vector <AtlasInfo> build();
	
private:
	std::vector <SpriteData> ims;
};



/// Wrapper around AtlasPacker, filling actual pixels
class AtlasBuilder
{
public:
	struct AtlasData
	{
		AtlasPacker::AtlasInfo info;
		std::vector <uint8_t> px;
	};
	
	/// Must be not null
	std::shared_ptr <AtlasPacker> pk = nullptr;
	
	
	
	AtlasBuilder() = default;
	~AtlasBuilder();
	
	/// Frees memory and clears packer
	void reset();
	
	/// Memory remains valid until build
	void add_static( const AtlasPacker::InputInfo& info, const void *data, size_t pitch = 0 );
	
	/// Copies memory
	void add_copy( const AtlasPacker::InputInfo& info, const void *data, size_t pitch = 0 );
	
	/// Assumes ownership of memory allocated with malloc/realloc
	void add_move_malloc( const AtlasPacker::InputInfo& info, void *data, size_t pitch = 0 );
	
	/// Assumes ownership of memory allocated with new[]
	void add_move_new( const AtlasPacker::InputInfo& info, void *data, size_t pitch = 0 );
	
	/// Make atlases and fills data
	std::vector <AtlasData> build();
	
private:
	struct Data
	{
		uint64_t id;
		size_t pitch;
		uint8_t *px;
		int type;
	};
	std::vector <Data> ds;
	void add( const AtlasPacker::InputInfo& info, const void *data, size_t pitch, int type );
};

#endif // VAS_ATLAS_PACKER_HPP
