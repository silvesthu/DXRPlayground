#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "tinygltf/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tinygltf/stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"

#ifdef _MSC_VER
// C4100: tinybvh keeps currently-unused extension parameters in public/internal APIs.
// C4189: tinybvh leaves debug/SAH temporaries live in code paths disabled by macros.
// C4458: tinybvh uses parameter names that intentionally mirror member names.
#pragma warning(push)
#pragma warning(disable: 4100 4189 4458)
#endif
#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
