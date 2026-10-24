/* Compiles stb_vorbis in its own library, without the project's warning
   flags (third-party code), and wraps the few calls OpenDay needs. */
#include "stb_vorbis.c"

#include "vorbis_wrapper.h"

void* openday_vorbis_open(const char* path, int* channels, int* rate) {
    int error = 0;
    stb_vorbis* f = stb_vorbis_open_filename(path, &error, 0);
    if (!f) return 0;
    stb_vorbis_info info = stb_vorbis_get_info(f);
    *channels = info.channels;
    *rate = (int)info.sample_rate;
    return f;
}

int openday_vorbis_read(void* f, int channels, short* buffer, int shorts) {
    return stb_vorbis_get_samples_short_interleaved((stb_vorbis*)f, channels, buffer, shorts);
}

int openday_vorbis_rewind(void* f) { return stb_vorbis_seek_start((stb_vorbis*)f); }

void openday_vorbis_close(void* f) { stb_vorbis_close((stb_vorbis*)f); }
