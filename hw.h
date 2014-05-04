#ifndef __HW_H__
#define __HW_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t *keystate;
extern uint8_t keypressed[256];
extern uint8_t keyreleased[256];

void hw_init(int audiofreq, int fps);
int hw_poll(void);

void hw_beginframe(uint8_t **screen, int *pitch);
void hw_endframe();

void hw_beginaudio(int16_t **buf, int *nsamples);
void hw_endaudio(void);

#ifdef __cplusplus
}
#endif

#endif

