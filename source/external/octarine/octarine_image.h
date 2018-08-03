#pragma once
#include <stdint.h>

#ifdef __cplusplus
	#define OCTARINE_IMAGE_API extern "C"
	#pragma comment(lib, "octarine_image.lib") 
#else
	#define OCTARINE_IMAGE_API
#endif // __cplusplus

enum DXGI_FORMAT;

typedef enum OCTARINE_IMAGE {
	OCTARINE_IMAGE_OK = 0,
	OCTARINE_IMAGE_FAIL_FILE_ERROR_OPEN,
	OCTARINE_IMAGE_FAIL_FILE_ERROR_CLOSE,
	OCTARINE_IMAGE_FAIL_FILE_ERROR_READ,
	OCTARINE_IMAGE_FAIL_FILE_ERROR_WRITE,
	OCTARINE_IMAGE_FAIL_MEMORY_ERROR_ALLOCATE,
	OCTARINE_IMAGE_FAIL_TYPE_ERROR,
} OCTARINE_IMAGE;

typedef enum OCTARINE_IMAGE_FORMAT_TYPE {
	OCTARINE_IMAGE_FORMAT_TYPELESS = 0,
	OCTARINE_IMAGE_FORMAT_FLOAT = 1,
	OCTARINE_IMAGE_FORMAT_UNSIGNED_FLOAT_16 = 2,
	OCTARINE_IMAGE_FORMAT_UNORM = 3,

	OCTARINE_IMAGE_FORMAT_TYPEMAX = 15
} OCTARINE_IMAGE_FORMAT_TYPE;

typedef enum OCTARINE_IMAGE_FORMAT_FLAG {
	OCTARINE_IMAGE_FORMAT_FLAG_BLOCK_COMPRESSED = 0x0001,
	
	OCTARINE_IMAGE_FORMAT_FLAG_FLAGMAX = 0xFFFF
} OCTARINE_IMAGE_FORMAT_FLAG;

typedef enum OCTARINE_IMAGE_FORMAT {
	OCTARINE_IMAGE_UNKNOWN = 0,
	
	OCTARINE_IMAGE_R16G16_FLOAT = 0x0000'2'20'1,

	OCTARINE_IMAGE_R8G8B8A8_UNORM = 0x0000'4'20'3,
	
	OCTARINE_IMAGE_R16B16G16A16_FLOAT = 0x0000'4'40'1,

	OCTARINE_IMAGE_R32B32G32A32_TYPELESS = 0x0000'4'80'0,
	
	OCTARINE_IMAGE_R32B32G32A32_FLOAT	 = 0x0000'4'80'1,
	
	OCTARINE_IMAGE_BC6H_UF16 = 0x0001'1'80'2,

	OCTARINE_IMAGE_R8G8B8A8_UNORM_SRGB = 0x0002'4'20'3,

	OCTARINE_IMAGE_FORMAT_MAX = 0xFFFF'F'FF'F
} OCTARINE_IMAGE_FORMAT;

typedef enum OCTARINE_IMAGE_FLAGS {
	OCTARINE_IMAGE_FLAGS_CUBE = 0x1,
	OCTARINE_IMAGE_FLAGS_SRGB = 0x2,
	OCTARINE_IMAGE_FLAGS_MAX = 0xFFFF
} OCTARINE_IMAGE_FLAGS;

typedef struct OctarineImageFormat {
	union {
		OCTARINE_IMAGE_FORMAT as_enum;
		struct {
			uint32_t type				: 4;
			uint32_t num_bits_per_pixel : 8;
			uint32_t num_channels		: 4;			
			uint32_t flags				: 16;	
		};
	};
} OctarineImageFormat;

typedef struct OctarineImageHeader {
	size_t	size_of_data;
	OctarineImageFormat format;
	uint16_t	width;
	uint16_t	height;
	uint16_t	depth;
	uint16_t	array_size;
	uint16_t    mip_levels;
	uint16_t    flags;
} OctarineImageHeader;

OCTARINE_IMAGE_API DXGI_FORMAT octarine_image_get_dxgi_format(OCTARINE_IMAGE_FORMAT e_format);

OCTARINE_IMAGE_API OctarineImageFormat octarine_image_make_format(OCTARINE_IMAGE_FORMAT_TYPE format_type, uint8_t num_bits_per_pixel, uint8_t num_channels, uint16_t flags);

OCTARINE_IMAGE_API OctarineImageFormat octarine_image_make_format_from_dxgi_format(DXGI_FORMAT format);

OCTARINE_IMAGE_API void octarine_image_get_subresource_infos(OctarineImageHeader *p_header, uint64_t *p_subresource_offsets, uint64_t *p_subresource_sizes, uint64_t *p_subresource_row_sizes);

OCTARINE_IMAGE_API OCTARINE_IMAGE octarine_image_read_from_file(const char *p_file_name, OctarineImageHeader *p_header, void **pp_data);

OCTARINE_IMAGE_API OCTARINE_IMAGE octarine_image_write_to_file(const char *p_file_name, OctarineImageHeader *p_header, const void *p_data);

