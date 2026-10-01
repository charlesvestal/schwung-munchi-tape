/* surface.h -- Move's controls, played as a CHOMPI.
 *
 * Every Move control is translated into the CHOMPI key or encoder it stands
 * for, and the engine (munchi_engine.cpp, the firmware's own page logic)
 * decides what that means. This file owns only the translation, the lights
 * and the screen. The full map is in README.md; in short:
 *
 *   pads          the 25-key keyboard, two octaves as piano rows
 *   Shift         the CHOMPI key in play mode: hold for the menu layer, where
 *                 the same keys are modes, banks, inputs, erase/copy/save
 *   Sample        the CHOMPI key in record mode: record into the buffer
 *   Loop / Play   the looper's two keys (Rec is a second Loop)
 *   knobs 1-8     Speed, Start, End, Space, Lofi, Filter, Loop speed, Input;
 *                 Left/Right flip knobs 1-3 to Gain, Attack, Decay
 *   volume knob   Volume;  Shift + any knob = the menu page's encoder
 *   Delete+touch  an encoder click (reset pitch / FX / loop speed)
 *   Track 1-4     auto-loop, sustain, monitor mode, record switch
 *   steps 1-15    slots (Keys: select, Kit: play); jog browses slots
 *   Up/Down       keyboard octave;  Menu / jog click: settings;  Back x2: exit
 */
#pragma once
#include <stdint.h>
#include "display.h"
#include "../engine/munchi_engine.h"

namespace munchi
{

class Surface
{
  public:
    explicit Surface(MunchiEngine &e);

    /** A cable-0 event from Move's own controls. */
    void HandleInternal(uint8_t status, uint8_t d1, uint8_t d2);

    /** Once per SPI transfer: LEDs + MIDI out into MIDI_OUT, one display
     *  slice. `spi` is the mapped transfer buffer, output region cleared. */
    void Tick(uint8_t *spi);

    /** Force every LED to be re-sent (start-up, after a clear). */
    void InvalidateLeds();

    bool WantsExit() const { return exit_; }

    /* diagnostics: what Move's jack-detect CC said, for the log */
    int jack_cc_value_ = -1;
    int jack_cc_seen_  = 0;

    /** Turn everything off, 20 packets at a time: used on the way out.
     *  Returns the index to continue from, or -1 when done. */
    int WriteAllOff(uint8_t *spi, int start);

  private:
    enum
    {
        kNumPads = 32,
    };

    int  PadToKey(int pad) const;
    void OnPad(int pad, int vel, bool on);
    void OnStep(int step, bool on);
    void OnKnob(int knob, int delta);
    void OnKnobTouch(int knob, bool on);
    void OnButton(int cc, bool press);
    void Show(const char *name, const char *value);

    void ComputeLeds();
    uint8_t NormalKeyColor(int id);
    uint8_t MenuKeyColor(int id);
    uint8_t BankColor(bool dim);

    void Draw();
    void DrawMain();
    void DrawSettings();
    void DrawSampleBar(int y);
    void DrawLooper(int y);
    void KnobText(int knob, bool menu, char *name, char *value);

    void SettingsActivate(int dir);
    int  NextSlot(int from, int dir);

    MunchiEngine &eng_;
    Display       disp_;

    bool shift_ = false, delete_ = false;
    int  page_  = 0; // knob page for knobs 1-3 (CHOMPI encoders A-C)
    int  octave_ = 0;
    int  pad_hold_[40] = {0};
    int  step_key_[16];
    bool step_held_[16] = {false};

    char     show_name_[24] = {0}, show_value_[24] = {0};
    uint32_t show_t_ = 0;
    int      touched_knob_ = -1;

    bool     settings_ = false;
    int      settings_cursor_ = 0;
    uint32_t exit_prompt_t_ = 0;
    bool     exit_prompt_ = false;
    bool     exit_ = false;

    /* LEDs: wanted vs sent */
    uint8_t want_pad_[kNumPads], sent_pad_[kNumPads];
    uint8_t want_step_[16], sent_step_[16];
    uint8_t want_cc_[128], sent_cc_[128];
    bool    cc_used_[128] = {false};
    int     refresh_i_ = 0;
    uint32_t tick_ = 0;
};

} // namespace munchi
