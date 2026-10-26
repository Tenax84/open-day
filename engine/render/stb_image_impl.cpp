// Compiles stb_image (PNG only) in its own library, without the project's
// warning flags (third-party code): the pictures of an HD interface pack.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
