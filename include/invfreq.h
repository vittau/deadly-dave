#ifndef _INVFREQ_H_
#define _INVFREQ_H_

#include <stddef.h>
#include <stdint.h>

size_t invfreq_decode_soundfx(const uint16_t *data, uint8_t *out, int samples_per_beep);

#endif
