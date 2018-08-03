#pragma once
#include <stdint.h>

#ifdef __cplusplus
#define OCTARINE_MESH_API extern "C"
#pragma comment(lib, "octarine_mesh.lib") 
#else
#define OCTARINE_MESH_API
#endif // __cplusplus

typedef enum OCTARINE_MESH_RESULT {
	OCTARINE_MESH_OK = 0,
	OCTARINE_MESH_FAIL_FILE_ERROR_OPEN,
	OCTARINE_MESH_FAIL_FILE_ERROR_CLOSE,
	OCTARINE_MESH_FAIL_FILE_ERROR_READ,
	OCTARINE_MESH_FAIL_FILE_ERROR_WRITE,
	OCTARINE_MESH_FAIL_MEMORY_ERROR_ALLOCATE,
	OCTARINE_MESH_FAIL_TYPE_ERROR,
} OCTARINE_MESH_RESULT;

typedef struct OctarineMeshHeader {
	uint32_t	size_of_data;
	uint32_t	num_vertices;
	uint32_t	num_indices;
} OctarineMeshHeader;

OCTARINE_MESH_API OCTARINE_MESH_RESULT octarine_mesh_read_from_file(const char *p_file_name, OctarineMeshHeader *p_header, void **pp_data);

OCTARINE_MESH_API OCTARINE_MESH_RESULT octarine_mesh_write_to_file(const char *p_file_name, OctarineMeshHeader *p_header, const void *p_data);