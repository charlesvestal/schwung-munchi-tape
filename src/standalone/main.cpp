/* Munchi Tape -- a standalone Schwung tool.
 *
 * Launched from Schwung's Tools menu by launch-standalone.sh, which stops Move
 * and frees /dev/ablspi0.0. From then until Back is pressed twice this binary
 * IS the instrument: it runs the SPI transfer loop itself, one 128-frame block
 * per transfer, exactly as the CHOMPI's Daisy ran its audio callback. When it
 * exits, the launcher restarts Move.
 *
 * One thread does SPI, audio and controls (the firmware's shape); the card
 * worker (munchi_engine.cpp) does every file operation beside it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string>

#ifndef _IOC_NONE
#define _IOC_NONE 0U
#endif
#ifndef _IOC
#define _IOC(dir, type, nr, size) \
    (((dir) << 30) | ((type) << 8) | ((nr) << 0) | ((size) << 16))
#endif

#include "../schwung_spi_lib.h"
#include "../engine/munchi_engine.h"
#include "surface.h"

using namespace munchi;

static volatile sig_atomic_t g_running = 1;
static void on_signal(int) { g_running = 0; }

static const char *kDefaultCard = "/data/UserData/UserLibrary/Samples/Schwung/Munchi Tape";

static std::string module_dir()
{
    char    buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(n <= 0)
        return ".";
    buf[n]           = 0;
    std::string p    = buf;
    size_t      slash = p.rfind('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

static void log_line(const char *msg)
{
    // The unified log, only when the user has switched it on.
    if(access("/data/UserData/schwung/debug_log_on", F_OK) != 0)
        return;
    FILE *f = fopen("/data/UserData/schwung/debug.log", "a");
    if(!f)
        return;
    fprintf(f, "munchi-tape: %s\n", msg);
    fclose(f);
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    std::string mod     = module_dir();
    const char *card_env = getenv("MUNCHI_CARD");
    std::string card    = card_env && *card_env ? card_env : kDefaultCard;

    // This tool was first published as "munchi", whose card was
    // Samples/Schwung/Munchi. Adopt that folder (and whatever was saved to
    // it) rather than seeding a second copy of the factory card beside it.
    if(!card_env || !*card_env)
    {
        const std::string legacy = "/data/UserData/UserLibrary/Samples/Schwung/Munchi";
        if(access(card.c_str(), F_OK) != 0 && access((legacy + "/.munchi-card").c_str(), F_OK) == 0)
            rename(legacy.c_str(), card.c_str());
    }

    static MunchiEngine engine; // large: keep it off the stack
    engine.LoadOptions(card + "/options.json");
    if(engine.Init() != 0)
    {
        log_line("out of memory at init");
        return 1;
    }
    engine.OptionsChanged();

    // Headphones or speakers: Move only reports the jack when it changes, and
    // Schwung saves the last report here (shim_worker.c, JACK_STATE_PATH).
    // Unknown counts as speakers -- the side that cannot feed back.
    if(FILE *jf = fopen("/data/UserData/schwung/jack_state", "r"))
    {
        int v = -1;
        if(fscanf(jf, "%d", &v) == 1)
            engine.SetHeadphones(v == 127);
        fclose(jf);
    }
    engine.StartCardWorker(card, mod + "/card");
    log_line(("card " + card).c_str());

    int fd = open(SCHWUNG_SPI_DEVICE, O_RDWR);
    if(fd < 0)
    {
        log_line("cannot open SPI device");
        engine.StopCardWorker();
        return 1;
    }
    uint8_t *spi = (uint8_t *)mmap(nullptr, SCHWUNG_PAGE_SIZE,
                                   PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(spi == MAP_FAILED)
    {
        log_line("cannot map SPI device");
        close(fd);
        engine.StopCardWorker();
        return 1;
    }
    ioctl(fd, _IOC(_IOC_NONE, 0, SCHWUNG_IOCTL_SET_SPEED, 0), SCHWUNG_SPI_FREQ);
    const unsigned long xfer = _IOC(_IOC_NONE, 0, SCHWUNG_IOCTL_WAIT_SEND_SIZE, 0);

    // The SPI loop is the audio thread: realtime, on the core Move used for
    // it. Both calls may be refused (no capability); it still runs.
    {
        struct sched_param sp;
        sp.sched_priority = 70;
        sched_setscheduler(0, SCHED_FIFO, &sp);
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(3, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }

    Surface surface(engine);
    int     jack_logged = 0;

    while(g_running && !surface.WantsExit())
    {
        // MIDI in from the last transfer
        const uint8_t *in = spi + SCHWUNG_OFF_IN_MIDI;
        for(int i = 0; i < SCHWUNG_MIDI_IN_MAX; i++)
        {
            const uint8_t *ev = in + i * 8;
            if(!ev[0] && !ev[1] && !ev[2] && !ev[3])
                break; // an empty slot ends the list
            uint8_t cable = ev[0] >> 4;
            uint8_t cin   = ev[0] & 0x0F;
            if(cin < 0x08 || cin > 0x0E)
                continue;
            if(cable == 0)
                surface.HandleInternal(ev[1], ev[2], ev[3]);
            else if(cable == 2)
                engine.Midi(ev + 1, 3);
        }

        // audio in (from the last transfer) -> engine -> audio out
        int16_t audio_in[SCHWUNG_AUDIO_FRAMES * 2];
        memcpy(audio_in, spi + SCHWUNG_OFF_IN_AUDIO, sizeof(audio_in));
        memset(spi, 0, SCHWUNG_OFF_IN_BASE);
        engine.ProcessHostBlock(audio_in, (int16_t *)(spi + SCHWUNG_OFF_OUT_AUDIO),
                                SCHWUNG_AUDIO_FRAMES);

        surface.Tick(spi);

        if(surface.jack_cc_seen_ != jack_logged)
        {
            jack_logged = surface.jack_cc_seen_;
            char m[64];
            snprintf(m, sizeof(m), "jack detect CC114 = %d", surface.jack_cc_value_);
            log_line(m); // rare: only on a jack change
        }

        if(ioctl(fd, xfer, SCHWUNG_FRAME_SIZE) < 0 && g_running)
        {
            // EINTR on the way out is expected; anything else ends the session
            break;
        }
    }

    log_line("exiting");

    // lights off, screen blank, silence -- then hand the device back
    for(int next = 0; next >= 0;)
    {
        memset(spi, 0, SCHWUNG_OFF_IN_BASE);
        next = surface.WriteAllOff(spi, next);
        ioctl(fd, xfer, SCHWUNG_FRAME_SIZE);
    }
    for(int p = 0; p < 7; p++)
    {
        memset(spi, 0, SCHWUNG_OFF_IN_BASE);
        if(p > 0)
            spi[SCHWUNG_OFF_OUT_DISP_STAT] = (uint8_t)p;
        ioctl(fd, xfer, SCHWUNG_FRAME_SIZE);
    }

    engine.StopCardWorker(); // flushes presets.json / options.json

    munmap(spi, SCHWUNG_PAGE_SIZE);
    close(fd);
    return 0;
}
