/* surface_test.cpp -- Move controls in, CHOMPI state out, no device.
 *
 * Turns each knob on each page (and with Shift) through Surface exactly as the
 * SPI loop would deliver it, and checks the parameter named on the screen is
 * the one that moved. Prints the screen as ASCII for a look.
 */
#include "../src/standalone/surface.h"
#include <stdio.h>
#include <string.h>

using namespace munchi;

static MunchiEngine eng;
static int16_t in[256], out[256];

static void run_ms(int ms)
{
    for(int i = 0; i < ms * 441 / 1280 + 1; i++)
        eng.ProcessHostBlock(in, out, 128);
}

static void dump(Surface &s)
{
    const uint8_t *px = s.TestScreen();
    for(int y = 0; y < 64; y += 2)
    {
        for(int x = 0; x < 128; x++)
        {
            int a = px[y * 128 + x], b = px[(y + 1) * 128 + x];
            putchar(a && b ? '#' : a ? '"' : b ? '.' : ' ');
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    eng.Init();
    if(argc > 1)
        eng.LoadCardBlocking(argv[1]);
    Surface s(eng);
    run_ms(1600); // the firmware ignores input for 1.5 s

    int fails = 0;
    auto turn = [&](int knob, int detents) {
        s.HandleInternal(0xB0, knob == 8 ? 79 : 71 + knob, detents > 0 ? detents : 128 + detents);
        run_ms(20);
    };
    auto expect = [&](const char *what, float before, float after) {
        bool ok = after != before;
        printf("%-28s %.3f -> %.3f %s\n", what, before, after, ok ? "ok" : "DID NOT MOVE");
        fails += !ok;
    };

    // page 1
    float v;
    v = eng.EncValue(0, 0); turn(0, -5); expect("p1 k1 speed", v, eng.EncValue(0, 0));
    v = eng.EncValue(0, 1); turn(1, 5); expect("p1 k2 start", v, eng.EncValue(0, 1));
    v = eng.EncValue(0, 2); turn(2, -5); expect("p1 k3 end", v, eng.EncValue(0, 2));
    v = eng.EncValue(1, 1); turn(3, 5); expect("p1 k4 attack", v, eng.EncValue(1, 1));
    v = eng.EncValue(1, 2); turn(4, 5); expect("p1 k5 decay", v, eng.EncValue(1, 2));
    v = eng.EncValue(0, 3); turn(5, 5); expect("p1 k6 space", v, eng.EncValue(0, 3));
    v = eng.EncValue(2, 3); turn(6, 5); expect("p1 k7 filter", v, eng.EncValue(2, 3));
    v = eng.Eng().GetLooperScrub(); turn(7, 5); run_ms(300);
    printf("%-28s (looper empty: scrubs) %s\n", "p1 k8 loop", "-");
    v = eng.EncValue(0, 5); turn(8, -5); expect("volume knob", v, eng.EncValue(0, 5));
    dump(s);

    // page 2
    s.HandleInternal(0xB0, 63, 127);
    v = eng.EncValue(1, 0); turn(0, -5); expect("p2 k1 gain", v, eng.EncValue(1, 0));
    v = eng.Eng().GetPan(); turn(1, 5); expect("p2 k2 pan", v, eng.Eng().GetPan());
    v = eng.EncValue(1, 3); turn(2, 5); expect("p2 k3 lofi", v, eng.EncValue(1, 3));
    v = eng.Warble(); turn(3, 5); expect("p2 k4 warble", v, eng.Warble());
    v = eng.DelayTime(); turn(4, 5); expect("p2 k5 delay time", v, eng.DelayTime());
    v = eng.Resonance(); turn(5, 5); expect("p2 k6 resonance", v, eng.Resonance());
    v = eng.EncValue(1, 5); turn(6, -5); expect("p2 k7 input", v, eng.EncValue(1, 5));
    v = eng.FinalComp(); turn(7, 5); expect("p2 k8 comp", v, eng.FinalComp());
    dump(s);

    // shift gestures, page 1
    s.HandleInternal(0xB0, 62, 127);
    s.HandleInternal(0xB0, 49, 127);
    run_ms(20);
    v = eng.EncValue(0, 1); float e = eng.EncValue(0, 2); turn(1, -1);
    expect("shift k2 window: start", v, eng.EncValue(0, 1));
    expect("shift k2 window: end", e, eng.EncValue(0, 2));
    v = eng.Eng().GetGlobalPitch(); turn(0, 8); expect("shift k1 speed snap", v, eng.Eng().GetGlobalPitch());
    s.HandleInternal(0xB0, 49, 0);
    run_ms(1500);

    printf("%s\n", fails ? "FAILED" : "all knobs move what they say");
    return fails ? 1 : 0;
}
