#include "hw.h"
#include <SDL.h>
#include <SDL_framerate.h>
#include <assert.h>
#include <time.h>

#define HW_AUDIO_NUMBUFFERS 8

static SDL_Surface *screen;
static SDL_Surface *frame;
static FPSmanager fps;
static int16_t AUDIO_BUF[HW_AUDIO_NUMBUFFERS][HW_AUDIO_NUMSAMPLES];
static int audio_buf_index_w=2, audio_buf_index_r=0;
uint8_t *keystate;
static clock_t frameclock;
static int framecounter;

#define SPLIT 20

static void fill_audio(void *userdata, uint8_t* stream, int len);

void hw_init(void)
{
    if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0 )
    {
        printf("Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);

    screen=SDL_SetVideoMode(288+224+SPLIT, 288, 32, SDL_DOUBLEBUF);
    if (screen == NULL)
    {
       printf("Unable to set video mode: %s\n", SDL_GetError());
       exit(1);
    }

    frame = SDL_CreateRGBSurface(SDL_SWSURFACE, 36*8, 28*8, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x0);
    SDL_initFramerate(&fps);
    SDL_setFramerate(&fps, 100);

    /* Initialize audio */
    SDL_AudioSpec wanted;

    wanted.freq = HW_AUDIO_FREQ;
    wanted.format = AUDIO_S16;
    wanted.channels = 1;
    wanted.samples = HW_AUDIO_NUMSAMPLES;
    wanted.callback = fill_audio;
    wanted.userdata = NULL;
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_PauseAudio(0);
}

int hw_poll(void)
{
    SDL_Event event;

    while ( SDL_PollEvent(&event) )
    {
        if (event.type == SDL_QUIT)
            return 0;

        if ( event.type == SDL_KEYDOWN )
        {
            if ( event.key.keysym.sym == SDLK_ESCAPE )
                return 0;
        }
    }

    keystate = SDL_GetKeyState(NULL);

    return 1;
}

void hw_beginframe(uint8_t **screen, int *pitch)
{
    SDL_LockSurface(frame);
    *screen = frame->pixels;
    *pitch = frame->pitch;
}

#define ARGB  1

void hw_endframe(void)
{
    SDL_LockSurface(screen);

    uint8_t *spixels = screen->pixels;
    uint8_t *fpixels = frame->pixels;
    int x,y;
    for (y=0;y<224;y++)
    {
        int ry = 223-y;
        uint8_t *framerow = fpixels + ry*frame->pitch;
        for (x=0;x<288;x++)
        {
            int sx = 288+SPLIT+y;
            int sy = x;

#if ARGB
            spixels[sy*screen->pitch + sx*4+1] = framerow[x*4];
            spixels[sy*screen->pitch + sx*4+2] = framerow[x*4+1];
            spixels[sy*screen->pitch + sx*4+3] = framerow[x*4+2];
            spixels[sy*screen->pitch + sx*4+0] = 0;

            spixels[ry*screen->pitch + x*4+1] = framerow[x*4];
            spixels[ry*screen->pitch + x*4+2] = framerow[x*4+1];
            spixels[ry*screen->pitch + x*4+3] = framerow[x*4+2];
            spixels[ry*screen->pitch + x*4+0] = 0;
#else
            spixels[sy*screen->pitch + sx*4+0] = framerow[x*4];
            spixels[sy*screen->pitch + sx*4+1] = framerow[x*4+1];
            spixels[sy*screen->pitch + sx*4+2] = framerow[x*4+2];
            spixels[sy*screen->pitch + sx*4+3] = 0;

            spixels[ry*screen->pitch + x*4+0] = framerow[x*4+0];
            spixels[ry*screen->pitch + x*4+1] = framerow[x*4+1];
            spixels[ry*screen->pitch + x*4+2] = framerow[x*4+2];
            spixels[ry*screen->pitch + x*4+3] = 0;
#endif
        }
    }

    SDL_UnlockSurface(frame);
    SDL_UnlockSurface(screen);

    if (!frameclock || SDL_GetTicks() < frameclock)
    {
        SDL_Flip(screen);
        //SDL_framerateDelay(&fps);
    }

    if (framecounter == 0)
        frameclock = SDL_GetTicks();
    frameclock += 1000/60+1;
    framecounter += 1;
}

void hw_beginaudio(int16_t **buf)
{
    extern int framecounter;
#if 0
    if (audio_buf_index_w >= audio_buf_index_r + HW_AUDIO_NUMBUFFERS)
        printf("[AUDIO](FC=%04d/R=%04d/W%04d) Warning: overflow audio buffer (producing too fast)\n", framecounter, audio_buf_index_r, audio_buf_index_w);
#endif
    *buf = AUDIO_BUF[audio_buf_index_w % HW_AUDIO_NUMBUFFERS];
}

void hw_endaudio(void)
{
    audio_buf_index_w += 1;
}

void fill_audio(void *userdata, uint8_t *stream, int len)
{
    static int audiocounter = 0;

    if (audio_buf_index_r == audio_buf_index_w)
    {
        #if 0
        extern int framecounter;
        printf("[AUDIO](FC=%04d/AC=%04d/W=%04d) Warning: no audio generated, silencing...\n", framecounter, audiocounter, audio_buf_index_w);
        #endif
        memset(stream, 0, len);
        return;
    }

    assert(sizeof(AUDIO_BUF) / HW_AUDIO_NUMBUFFERS == len);
    memcpy(stream, AUDIO_BUF[audio_buf_index_r++ % HW_AUDIO_NUMBUFFERS], len);
    ++audiocounter;
}
