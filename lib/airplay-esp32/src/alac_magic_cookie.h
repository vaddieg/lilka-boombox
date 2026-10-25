#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#include "audio_receiver.h"

#define ALAC_MAGIC_COOKIE_SIZE 24

void build_alac_magic_cookie(uint8_t *cookie, const audio_format_t *fmt);

#ifdef __cplusplus
}
#endif // extern C
