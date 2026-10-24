/* The Ogg Vorbis decoding OpenDay uses, implemented with stb_vorbis in
   stb_vorbis_impl.c. */
#ifndef OPENDAY_VORBIS_WRAPPER_H
#define OPENDAY_VORBIS_WRAPPER_H
#ifdef __cplusplus
extern "C" {
#endif

/* Opens a file; returns a handle or 0. */
void* openday_vorbis_open(const char* path, int* channels, int* rate);
/* Decodes interleaved 16-bit samples; returns frames per channel, 0 at the end. */
int openday_vorbis_read(void* f, int channels, short* buffer, int shorts);
int openday_vorbis_rewind(void* f);
void openday_vorbis_close(void* f);

#ifdef __cplusplus
}
#endif
#endif
