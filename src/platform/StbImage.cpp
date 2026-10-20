// The one compiled copy of the vendored stb_image decoder. In the domain library (not the retired frontend,
// where it used to live) because the online map's tile decoder (REQ-363, MapTileService) runs in
// gosurvey_domain and its tests, which have no window.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
