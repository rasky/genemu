#include "hw.h"
#include <SDL.h>
#include <assert.h>
#include <time.h>

#define HW_AUDIO_NUMBUFFERS 8

static SDL_Window *screen;
static SDL_Renderer *renderer;
static SDL_Texture *frame;
static uint8_t framebuf[320*224*4];

static int16_t AUDIO_BUF[HW_AUDIO_NUMBUFFERS][HW_AUDIO_NUMSAMPLES*2];
static int audio_buf_index_w=2, audio_buf_index_r=0;
const uint8_t *keystate;
static clock_t frameclock;
static int framecounter;

#define SPLIT 20
#define ZOOM 3

static void fill_audio(void *userdata, uint8_t* stream, int len);

void hw_init(void)
{
    if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0 )
    {
        printf("Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);

    SDL_CreateWindowAndRenderer(320*ZOOM, 224*ZOOM, SDL_WINDOW_RESIZABLE,
        &screen, &renderer);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");  // make the scaled rendering look smoother.
    SDL_RenderSetLogicalSize(renderer, 320, 224);

    frame = SDL_CreateTexture(renderer,
                              SDL_PIXELFORMAT_ABGR8888,
                              SDL_TEXTUREACCESS_STREAMING,
                              320, 224);

    keystate = SDL_GetKeyboardState(NULL);


#if 1
    /* Initialize audio */
    SDL_AudioSpec wanted;

    wanted.freq = HW_AUDIO_FREQ;
    wanted.format = AUDIO_S16;
    wanted.channels = 2;
    wanted.samples = HW_AUDIO_NUMSAMPLES;
    wanted.callback = fill_audio;
    wanted.userdata = NULL;
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_PauseAudio(0);
#endif
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

    return 1;
}

void hw_beginframe(uint8_t **screen, int *pitch)
{
    *screen = framebuf;
    *pitch = 320*4;
}

#define ARGB  1

void hw_endframe(void)
{

    if (!frameclock || SDL_GetTicks() < frameclock)
    {
        SDL_UpdateTexture(frame, NULL, framebuf, 320*4);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, frame, NULL, NULL);
        SDL_RenderPresent(renderer);

        if (frameclock > SDL_GetTicks())
            SDL_Delay(frameclock - SDL_GetTicks());
    }
    else
    {
        printf("HW: frameskipping %d (frameclock: %ld, ticks: %d)\n", framecounter, frameclock, SDL_GetTicks());
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
