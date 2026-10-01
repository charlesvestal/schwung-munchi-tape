#include "surface.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace munchi
{
using daisy::InputSource;
using daisy::MonitorMode;
using daisy::VoiceMode;
using daisy::kSlotNone;

/* ---- Move control numbers ---------------------------------------------- */
enum
{
    CC_JOG_CLICK = 3,
    CC_JOG       = 14,
    CC_TRACK4    = 40, // reversed: CC 43 is track 1
    CC_TRACK1    = 43,
    CC_SHIFT     = 49,
    CC_MENU      = 50,
    CC_BACK      = 51,
    CC_CAPTURE   = 52,
    CC_DOWN      = 54,
    CC_UP        = 55,
    CC_UNDO      = 56,
    CC_LOOP      = 58,
    CC_COPY      = 60,
    CC_LEFT      = 62,
    CC_RIGHT     = 63,
    CC_KNOB1     = 71,
    CC_VOLUME    = 79,
    CC_PLAY      = 85,
    CC_REC       = 86,
    CC_MUTE      = 88,
    CC_LINE_IN   = 114, // XMOS jack detect: 0 = internal mic, 127 = cable
    CC_LINE_OUT  = 115, // XMOS jack detect: 0 = speakers, 127 = headphones
    CC_SAMPLE    = 118,
    CC_DELETE    = 119,
};

/* ---- palette (src/shared/constants.mjs) --------------------------------- */
enum
{
    C_OFF       = 0,
    C_WHITE     = 120,
    C_GREY      = 118,
    C_GREY_DIM  = 123,
    C_GREY_DARK = 124,
    C_RED       = 127,
    C_GREEN     = 126,
    C_BLUE      = 125,
    C_YELLOW    = 7,
    C_ORANGE    = 3,
    C_PINK      = 25,
    C_PINK_DIM  = 113,
    C_PURPLE    = 23,
    C_TEAL      = 15,
    C_TEAL_DIM  = 93,
    C_AZURE     = 16,
    C_MAGENTA   = 26,
};

static const int kNumSettings = 10; // rows on the settings screen

// CHOMPI's five bank colours: purple, orange, teal, dark orange, yellow-green
static const uint8_t kBank[5]    = {23, 3, 15, 28, 9};
static const uint8_t kBankDim[5] = {109, 69, 93, 75, 81};

/* ---- the keyboard on the pads -------------------------------------------
 * Four rows of eight, bottom row first. White keys C3..C4 then C4..C5, each
 * with its black keys on the row above, sat between the whites they split.
 * C4 appears twice (end of the low row, start of the high row) because two
 * octaves of whites is 15 keys and a row is 8; both are the same CHOMPI key. */
static const int kPadNote[32] = {
    48, 50, 52, 53, 55, 57, 59, 60, // row 1
    -1, 49, 51, -1, 54, 56, 58, -1, // row 2
    60, 62, 64, 65, 67, 69, 71, 72, // row 3
    -1, 61, 63, -1, 66, 68, 70, -1, // row 4
};

// slot -> the white key that is that slot (inverse of KeyToSlot)
static const int kSlotKey[16] = {0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
                                 KEY_6, KEY_7, KEY_8, KEY_9, KEY_10,
                                 KEY_11, KEY_12, KEY_13, KEY_14, KEY_15};

static bool is_black(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

Surface::Surface(MunchiEngine &e) : eng_(e)
{
    for(int i = 0; i < 16; i++)
        step_key_[i] = -1;
    InvalidateLeds();
}

void Surface::InvalidateLeds()
{
    memset(sent_pad_, 0xFF, sizeof(sent_pad_));
    memset(sent_step_, 0xFF, sizeof(sent_step_));
    memset(sent_cc_, 0xFF, sizeof(sent_cc_));
}

int Surface::PadToKey(int pad) const
{
    if(pad < 0 || pad >= 32 || kPadNote[pad] < 0)
        return -1;
    return NoteToKeyId(kPadNote[pad]);
}

void Surface::Show(const char *name, const char *value)
{
    snprintf(show_name_, sizeof(show_name_), "%s", name);
    snprintf(show_value_, sizeof(show_value_), "%s", value);
    show_t_ = eng_.NowMs();
}

/* ---- input ---------------------------------------------------------------- */

void Surface::HandleInternal(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0;

    if(type == 0x90 || type == 0x80)
    {
        bool on = type == 0x90 && d2 > 0;
        if(d1 <= 9)
            OnKnobTouch(d1, on);
        else if(d1 >= 16 && d1 <= 31)
            OnStep(d1 - 16, on);
        else if(d1 >= 68 && d1 <= 99)
            OnPad(d1 - 68, d2, on);
        return;
    }
    if(type != 0xB0)
        return;

    if(d1 >= CC_KNOB1 && d1 < CC_KNOB1 + 8)
    {
        int delta = d2 < 64 ? d2 : (int)d2 - 128;
        OnKnob(d1 - CC_KNOB1, delta);
        return;
    }
    if(d1 == CC_VOLUME)
    {
        int delta = d2 < 64 ? d2 : (int)d2 - 128;
        OnKnob(8, delta);
        return;
    }
    if(d1 == CC_JOG)
    {
        int delta = d2 < 64 ? d2 : (int)d2 - 128;
        if(settings_)
        {
            settings_cursor_ += delta > 0 ? 1 : -1;
            if(settings_cursor_ < 0)
                settings_cursor_ = 0;
            if(settings_cursor_ > kNumSettings - 1)
                settings_cursor_ = kNumSettings - 1;
        }
        else if(eng_.Eng().GetVoiceMode() == VoiceMode::JAMMI)
        {
            int s = NextSlot(eng_.Eng().GetVoiceSlot(), delta > 0 ? 1 : -1);
            eng_.SelectSlot(s);
            char v[16];
            if(s == 15)
                snprintf(v, sizeof(v), "BUFFER");
            else
                snprintf(v, sizeof(v), "%c%d", 'A' + eng_.Eng().GetBank(), s);
            Show("SLOT", v);
        }
        return;
    }
    if(d1 == CC_LINE_OUT)
    {
        eng_.SetHeadphones(d2 != 0);
        return;
    }
    if(d1 == CC_LINE_IN)
    {
        // Logged, not obeyed: on hardware it said "cable" with none plugged
        // in, which put the mic on the line path. Mic/Line is the user's
        // choice (Shift + F#3 / G#3) until its meaning is measured.
        jack_cc_value_ = d2;
        jack_cc_seen_++;
        return;
    }
    OnButton(d1, d2 >= 64);
}

void Surface::OnPad(int pad, int vel, bool on)
{
    int id = PadToKey(pad);
    if(id < 0)
        return;
    if(on)
    {
        eng_.SetPadVelocity((float)vel);
        if(pad_hold_[id]++ == 0)
            eng_.Button(id, true);
    }
    else if(pad_hold_[id] > 0 && --pad_hold_[id] == 0)
    {
        eng_.Button(id, false);
    }
}

void Surface::OnStep(int step, bool on)
{
    int slot = step + 1;
    if(slot > 15)
        return;
    if(eng_.Eng().GetVoiceMode() == VoiceMode::JAMMI)
    {
        if(on)
            eng_.SelectSlot(slot);
        return;
    }
    // Kit: a step plays its slot, as that slot's white key would
    int id = kSlotKey[slot];
    if(on && !step_held_[step])
    {
        step_held_[step] = true;
        eng_.SetPadVelocity(127.f);
        if(pad_hold_[id]++ == 0)
            eng_.Button(id, true);
    }
    else if(!on && step_held_[step])
    {
        step_held_[step] = false;
        if(pad_hold_[id] > 0 && --pad_hold_[id] == 0)
            eng_.Button(id, false);
    }
}

/* Knob n -> CHOMPI encoder and page. The magic encoder's three pages each
 * get a knob; encoders A-C follow the Left/Right page; the volume knob is
 * encoder F's first page and knob 8 its second. */
void Surface::OnKnob(int knob, int delta)
{
    if(delta == 0)
        return;
    bool menu = eng_.MenuActive();
    int  enc = 0, page = 0;
    switch(knob)
    {
        case 0: enc = 0; page = page_; break;
        case 1: enc = 1; page = page_; break;
        case 2: enc = 2; page = page_; break;
        case 3: enc = 3; page = 0; break;
        case 4: enc = 3; page = 1; break;
        case 5: enc = 3; page = 2; break;
        case 6: enc = 4; page = 0; break;
        case 7: enc = 5; page = 1; break;
        case 8: enc = 5; page = 0; break;
    }
    eng_.Encoder(enc, page, delta, menu);
    touched_knob_ = knob;
    char name[24], value[24];
    KnobText(knob, menu, name, value);
    Show(name, value);
}

void Surface::OnKnobTouch(int knob, bool on)
{
    if(!on)
        return;
    if(delete_)
    {
        // Delete + touch = the encoder's click
        switch(knob)
        {
            case 0:
                eng_.Click(ENC_4_SW);
                Show(page_ ? "GAIN/PAN" : "SPEED", "RESET");
                break;
            case 3:
            case 4:
            case 5:
                eng_.Click(ENC_3_SW);
                Show("FX", "RESET");
                break;
            case 6:
                eng_.Click(ENC_5_SW);
                Show("LOOP SPEED", "RESET");
                break;
        }
        return;
    }
    if(knob > 8)
        return;
    touched_knob_ = knob;
    char name[24], value[24];
    KnobText(knob, eng_.MenuActive(), name, value);
    Show(name, value);
}

void Surface::OnButton(int cc, bool press)
{
    MunchiEngine &e = eng_;
    switch(cc)
    {
        case CC_SHIFT:
            shift_ = press;
            e.MenuKey(press);
            break;

        case CC_SAMPLE: e.RecordKey(press); break;

        case CC_LOOP:
        case CC_REC:
            if(press && delete_)
            {
                e.ClearLooper();
                Show("LOOPER", "CLEARED");
                break;
            }
            e.Button(KEY_28, press);
            break;

        case CC_PLAY: e.Button(KEY_27, press); break;

        case CC_DELETE:
            delete_ = press;
            if(shift_ || (e.MenuActive() && e.PresetMode() != 0))
                e.Button(KEY_23, press); // erase
            break;

        case CC_COPY:
            if(shift_ || e.MenuActive())
                e.Button(KEY_24, press);
            break;

        case CC_CAPTURE:
            if(shift_ || e.MenuActive())
                e.Button(KEY_25, press); // save
            break;

        case CC_LEFT:
        case CC_RIGHT:
            if(press)
            {
                page_ = cc == CC_RIGHT ? 1 : 0;
                for(int k = 0; k < 3; k++)
                    e.SetKnobPage(k, page_);
                Show("KNOBS 1-3", page_ ? "GAIN ATK DEC" : "SPD STA END");
            }
            break;

        case CC_UP:
        case CC_DOWN:
            if(press)
            {
                octave_ += cc == CC_UP ? 1 : -1;
                if(octave_ > 2)
                    octave_ = 2;
                if(octave_ < -2)
                    octave_ = -2;
                e.key_transpose = octave_ * 12;
                char v[8];
                snprintf(v, sizeof(v), "%+d", octave_);
                Show("OCTAVE", v);
            }
            break;

        case CC_TRACK1:
            if(press)
            {
                e.Click(ENC_1_SW);
                Show("AUTO LOOP", e.Eng().GetAutoLoop() ? "ON" : "OFF");
            }
            break;
        case CC_TRACK1 - 1:
            if(press)
            {
                e.Click(ENC_2_SW);
                Show("SUSTAIN", e.Eng().GetSustainActive() ? "ON" : "OFF");
            }
            break;
        case CC_TRACK1 - 2:
            if(press)
            {
                e.Click(ENC_6_SW);
                static const char *names[3] = {"DRY", "THRU FX", "SEND/RET"};
                int m = (int)e.Eng().GetMonitorMode();
                e.MutableOpts().monitor_pos = m;
                e.SaveOptions();
                Show("MONITOR", names[m % 3]);
            }
            break;
        case CC_TRACK4:
            if(press)
            {
                e.SetSwitch(!e.GetSwitch());
                if(e.GetSwitch())
                    Show("MONITOR", "OFF");
                else if(e.Eng().GetInputSource() == InputSource::MIC
                        && !e.Eng().GetHeadphones())
                    Show("MONITOR", "MIC: HEADPHONES");
                else
                    Show("MONITOR", "ON");
            }
            break;

        case CC_MENU:
        case CC_JOG_CLICK:
            if(!press)
                break;
            if(settings_ && cc == CC_JOG_CLICK)
                SettingsActivate(shift_ ? -1 : 1);
            else
                settings_ = !settings_;
            exit_prompt_ = false;
            break;

        case CC_BACK:
            if(!press)
                break;
            if(settings_)
            {
                settings_ = false;
                break;
            }
            if(exit_prompt_ && e.NowMs() - exit_prompt_t_ < 3000)
                exit_ = true;
            else
            {
                exit_prompt_   = true;
                exit_prompt_t_ = e.NowMs();
            }
            break;
    }
}

int Surface::NextSlot(int from, int dir)
{
    // 1..14 with a file, then 15 (the buffer), wrapping
    int s = from;
    for(int i = 0; i < 15; i++)
    {
        s += dir;
        if(s > 15)
            s = 1;
        if(s < 1)
            s = 15;
        if(s == 15 || eng_.Eng().GetFileExists(s - 1))
            return s;
    }
    return from;
}

/* ---- settings ----------------------------------------------------------- */

static const char *kSettingNames[10] = {
    "INPUT",       "RECORD LATCH", "TAPE SLEW",  "MONITOR",     "SPLIT DELAY",
    "SHIFT SNAP",  "PAD VELOCITY", "MIDI IN CH", "MIDI OUT CH", "EXIT",
};


void Surface::SettingsActivate(int dir)
{
    Options &o = eng_.MutableOpts();
    switch(settings_cursor_)
    {
        case 0:
        {
            // the same as Shift + F#3 / G#3 / A#3
            static const int keys[3] = {KEY_18, KEY_19, KEY_20};
            int cur = (int)eng_.Eng().GetInputSource() % 3;
            int key = keys[(cur + 3 + dir) % 3];
            eng_.Eng().SetInputSource(InputSource(key == KEY_18 ? 0 : key == KEY_19 ? 1 : 2));
            return; // not saved: the CHOMPI didn't either
        }
        case 1: o.record_latch = !o.record_latch; break;
        case 2: o.tape_slew = !o.tape_slew; break;
        case 3: o.monitor_pos = (o.monitor_pos + 3 + dir) % 3; break;
        case 4: o.split_delay = !o.split_delay; break;
        case 5: o.ps_quant = !o.ps_quant; break;
        case 6: eng_.key_velocity = eng_.key_velocity < 0.f ? 127.f : -1.f; break;
        case 7: o.midi_ch_in = (o.midi_ch_in + 16 + dir) % 16; break;
        case 8: o.midi_ch_out = (o.midi_ch_out + 16 + dir) % 16; break;
        case 9: exit_ = true; return;
    }
    eng_.OptionsChanged();
}

/* ---- text for a knob ------------------------------------------------------ */

static void pct(char *out, float v) { snprintf(out, 24, "%d%%", (int)lrintf(v * 100.f)); }

void Surface::KnobText(int knob, bool menu, char *name, char *value)
{
    daisy::Engine &x = eng_.Eng();
    auto speed = [&](float ratio, bool rev) {
        snprintf(value, 24, "%sx%.2f", rev ? "REV " : "", ratio);
    };
    if(menu)
    {
        switch(knob)
        {
            case 0:
                if(page_ == 0)
                {
                    strcpy(name, "SPEED SNAP");
                    speed(x.GetGlobalPitch(), x.GetReverse());
                }
                else
                {
                    strcpy(name, "PAN");
                    float p = x.GetPan();
                    if(fabsf(p - .5f) < .01f)
                        strcpy(value, "C");
                    else
                        snprintf(value, 24, "%s%d", p < .5f ? "L" : "R",
                                 (int)lrintf(fabsf(p - .5f) * 200.f));
                }
                return;
            case 1:
            case 2:
                if(page_ == 0)
                {
                    strcpy(name, "WINDOW");
                    snprintf(value, 24, "%d-%d%%", (int)(eng_.EncValue(0, 1) * 100),
                             (int)(eng_.EncValue(0, 2) * 100));
                }
                else
                {
                    strcpy(name, "ATK+DEC");
                    pct(value, eng_.EncValue(1, 1));
                }
                return;
            case 3:
                strcpy(name, "DELAY TIME");
                pct(value, eng_.DelayTime());
                return;
            case 4:
                strcpy(name, "WARBLE");
                pct(value, eng_.Warble());
                return;
            case 5:
                strcpy(name, "RESONANCE");
                pct(value, eng_.Resonance());
                return;
            case 6:
                strcpy(name, "LOOP SNAP");
                snprintf(value, 24, "x%.2f", x.GetLooperPitch());
                return;
            default:
                strcpy(name, "COMP");
                pct(value, eng_.FinalComp());
                return;
        }
    }
    switch(knob)
    {
        case 0:
            if(page_ == 0)
            {
                strcpy(name, "SPEED");
                speed(x.GetGlobalPitch(), x.GetReverse());
            }
            else
            {
                strcpy(name, "GAIN");
                float v = eng_.EncValue(1, 0);
                snprintf(value, 24, "x%.2f", 2.f * v * v + .01f);
            }
            return;
        case 1:
            if(page_ == 0)
            {
                strcpy(name, "START");
                pct(value, eng_.EncValue(0, 1));
            }
            else
            {
                strcpy(name, "ATTACK");
                float v = eng_.EncValue(1, 1);
                snprintf(value, 24, "%.2fs", (powf(v, 3.f) + .01f) * 20.f + .001f);
            }
            return;
        case 2:
            if(page_ == 0)
            {
                strcpy(name, "END");
                pct(value, eng_.EncValue(0, 2));
            }
            else
            {
                strcpy(name, "DECAY");
                float v = eng_.EncValue(1, 2);
                snprintf(value, 24, "%.2fs", (powf(v, 3.f) + .01f) * 4.f + .001f);
            }
            return;
        case 3:
            if(eng_.Opts().split_delay)
            {
                float v = eng_.EncValue(0, 3);
                strcpy(name, v < .5f ? "DELAY" : "REVERB");
                pct(value, v < .5f ? (.5f - v) * 2.f : (v - .5f) * 2.f);
            }
            else
            {
                strcpy(name, "SPACE");
                pct(value, eng_.EncValue(0, 3));
            }
            return;
        case 4:
            strcpy(name, "LOFI");
            pct(value, eng_.EncValue(1, 3));
            return;
        case 5:
        {
            strcpy(name, "FILTER");
            float v = eng_.EncValue(2, 3);
            if(fabsf(v - .5f) < .02f)
                strcpy(value, "OPEN");
            else
                snprintf(value, 24, "%s %d", v < .5f ? "LP" : "HP",
                         (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case 6:
            strcpy(name, "LOOP SPEED");
            if(x.IsLooperPlaying())
                snprintf(value, 24, "x%.2f", x.GetLooperPitch());
            else
                strcpy(value, "SCRUB");
            return;
        case 7:
            strcpy(name, "INPUT");
            pct(value, eng_.EncValue(1, 5));
            return;
        default:
            strcpy(name, "VOLUME");
            pct(value, eng_.EncValue(0, 5));
            return;
    }
}

/* ---- LEDs ------------------------------------------------------------------ */

uint8_t Surface::BankColor(bool dim)
{
    int b = eng_.Eng().GetVoiceBank() % 5;
    return dim ? kBankDim[b] : kBank[b];
}

/* NormalPage::Draw, keyboard part */
uint8_t Surface::NormalKeyColor(int id)
{
    daisy::Engine &x = eng_.Eng();
    size_t slot = daisy::KeyToSlot(id);
    bool   pink = !(x.GetVoiceSlot() != 15 || x.GetVoiceMode() == VoiceMode::CUBBI);
    bool   perm = eng_.GetSwitch() || x.GetInputSource() != InputSource::MIC;

    if(eng_.KeyIsPlaying(id))
        return C_WHITE;
    if(x.GetVoiceMode() == VoiceMode::CUBBI && perm)
    {
        if(slot != kSlotNone && x.GetFileExists(slot - 1) && id == KEY_15)
            return C_PINK_DIM;
        if(slot != kSlotNone && x.GetFileExists(slot - 1))
            return pink ? C_PINK_DIM : BankColor(true);
    }
    else if(x.GetVoiceMode() == VoiceMode::JAMMI
            && (id == KEY_1 || id == KEY_8 || id == KEY_15) && perm)
        return pink ? C_PINK_DIM : BankColor(true);

    // Munchi: the pads have no keys printed on them, so draw the keyboard
    int note = KeyIdToNote(id);
    return is_black(note) ? C_GREY_DARK : C_GREY_DIM;
}

/* MenuPage::Draw, keyboard part */
uint8_t Surface::MenuKeyColor(int id)
{
    daisy::Engine &x    = eng_.Eng();
    const int      pm   = eng_.PresetMode();
    const bool     blink = (eng_.NowMs() / 250) & 1;
    const int      mode = (int)x.GetVoiceMode();

    switch(id)
    {
        case KEY_16: return mode == 0 ? BankColor(false) : C_OFF;
        case KEY_17: return mode == 1 ? BankColor(false) : C_OFF;
        case KEY_18:
        case KEY_19:
        case KEY_20:
            return (int)x.GetInputSource() == id - KEY_18 + (id == KEY_20 ? -6 : 0)
                       ? C_PINK
                       : C_OFF;
        case KEY_21: return x.GetFxPreLooper() ? C_YELLOW : C_OFF;
        case KEY_22: return !x.GetFxPreLooper() ? C_YELLOW : C_OFF;
        case KEY_23:
            return (pm == MunchiEngine::PM_ERASE_SEL || pm == MunchiEngine::PM_ERASING
                    || pm == MunchiEngine::PM_NONE)
                       ? C_RED
                       : C_OFF;
        case KEY_24:
            return (pm == MunchiEngine::PM_COPY_SRC || pm == MunchiEngine::PM_COPY_DEST
                    || pm == MunchiEngine::PM_COPYING || pm == MunchiEngine::PM_NONE)
                       ? C_GREEN
                       : C_OFF;
        case KEY_25:
            return (pm == MunchiEngine::PM_SAVE_SEL || pm == MunchiEngine::PM_SAVING
                    || pm == MunchiEngine::PM_NONE)
                       ? C_BLUE
                       : C_OFF;
    }

    size_t i = daisy::KeyToSlot(id);
    if(i == kSlotNone)
        return C_OFF;
    const int sel = eng_.SelectedSlot();
    const int src = eng_.CopySrc();

    if(pm == MunchiEngine::PM_SAVE_SEL && i == 15)
        return C_PINK;
    if((int)i == sel && pm == MunchiEngine::PM_SAVE_SEL)
        return C_BLUE;
    if((int)i == sel && pm == MunchiEngine::PM_ERASE_SEL)
        return C_RED;
    if((int)i == sel && pm == MunchiEngine::PM_COPY_DEST)
        return C_BLUE;
    if((int)i == src && pm == MunchiEngine::PM_COPY_DEST)
        return C_GREEN;
    if((((size_t)x.GetBank() == x.GetVoiceBank() && x.GetVoiceSlot() == i)
        || (x.GetVoiceSlot() == 15 && i == 15))
       && pm == MunchiEngine::PM_NONE && mode == 0)
        return C_WHITE;
    if(pm == MunchiEngine::PM_SAVE_SEL || pm == MunchiEngine::PM_ERASE_SEL
       || pm == MunchiEngine::PM_COPY_SRC || pm == MunchiEngine::PM_COPY_DEST)
    {
        if(!blink)
            return C_OFF;
        if(pm == MunchiEngine::PM_ERASE_SEL && i == 15)
            return C_OFF;
        if(i == 15 && x.GetFileExists(i - 1))
            return C_PINK_DIM;
        if(x.GetFileExists(i - 1))
            return BankColor(true);
        if(pm == MunchiEngine::PM_SAVE_SEL || pm == MunchiEngine::PM_COPY_DEST)
            return C_GREY;
        return C_OFF;
    }
    if(i == 15 && x.GetFileExists(i - 1))
        return C_PINK;
    if(x.GetFileExists(i - 1))
        return BankColor(false);
    return C_OFF;
}

void Surface::ComputeLeds()
{
    daisy::Engine &x    = eng_.Eng();
    const bool     menu = eng_.MenuActive();
    const uint32_t now  = eng_.NowMs();

    for(int p = 0; p < 32; p++)
    {
        int id       = PadToKey(p);
        want_pad_[p] = id < 0 ? C_OFF : (menu ? MenuKeyColor(id) : NormalKeyColor(id));
    }

    // steps: slots 1-14 + the buffer
    for(int s = 0; s < 16; s++)
    {
        int     slot = s + 1;
        uint8_t c    = C_OFF;
        if(slot <= 15)
        {
            bool exists = x.GetFileExists(slot - 1);
            bool cur    = x.GetVoiceMode() == VoiceMode::JAMMI
                       && x.GetVoiceSlot() == (size_t)slot
                       && (slot == 15 || (size_t)x.GetBank() == x.GetVoiceBank());
            if(x.GetVoiceMode() == VoiceMode::CUBBI && eng_.KeyIsPlaying(kSlotKey[slot]))
                cur = true;
            if(cur)
                c = C_WHITE;
            else if(exists)
                c = slot == 15 ? C_PINK_DIM : BankColor(true);
        }
        want_step_[s] = c;
    }

    memset(want_cc_, 0, sizeof(want_cc_));
    auto set = [&](int cc, uint8_t v) {
        want_cc_[cc] = v;
        cc_used_[cc] = true;
    };

    // play key (NormalPage::Draw)
    uint8_t play = C_OFF, loop = C_OFF;
    if(x.GetLooperIsEmpty() && !x.IsLooperRecordArmed())
        play = C_OFF;
    else if(x.IsLooperRecordArmed())
        play = C_WHITE;
    else if(x.IsLooperFirstRecording() && x.IsLooperRecording())
        play = C_TEAL;
    else if(x.IsLooperPlaying())
        play = x.GetLooperPosition() < .5f ? C_TEAL : C_TEAL_DIM;
    else
        play = C_GREY;

    // loop key
    if(x.GetLooperIsEmpty() && !x.IsLooperRecordArmed())
        loop = C_OFF;
    else if(x.IsLooperRecordArmed())
        loop = ((now / 300) & 1) ? C_RED : C_OFF;
    else if(x.IsLooperFirstRecording() && x.IsLooperRecording())
        loop = C_RED;
    else if(x.IsLooperRecording())
        loop = C_YELLOW;
    else
        loop = C_GREY;
    set(CC_PLAY, play);
    set(CC_LOOP, loop);
    set(CC_REC, loop);

    // the CHOMPI key's record role
    uint8_t smp = C_OFF;
    if(eng_.Copying())
        smp = ((now / 300) & 1) ? C_PINK : C_OFF;
    else if(x.Recording())
        smp = C_RED;
    else if(!eng_.GetSwitch())
    {
        float vu = x.GetVUSample(daisy::VUTarget::VU_INPUT);
        smp      = vu < .15f ? C_GREY_DARK : vu < .5f ? C_GREEN : vu < .85f ? C_YELLOW : C_PINK;
    }
    set(CC_SAMPLE, smp);

    set(CC_SHIFT, menu ? C_WHITE : C_OFF);
    set(CC_TRACK1, x.GetAutoLoop() ? C_WHITE : C_GREY_DARK);
    set(CC_TRACK1 - 1, x.GetSustainActive() ? C_WHITE : C_GREY_DARK);
    {
        MonitorMode m = x.GetMonitorMode();
        set(CC_TRACK1 - 2, m == MonitorMode::BOTH ? C_BLUE
                           : m == MonitorMode::HP ? C_ORANGE
                                                  : C_YELLOW);
    }
    set(CC_TRACK4, eng_.GetSwitch() ? C_GREY_DARK : C_RED);
    set(CC_LEFT, page_ == 1 ? C_WHITE : C_OFF);
    set(CC_RIGHT, page_ == 0 ? C_WHITE : C_OFF);
    set(CC_UP, octave_ < 2 ? (octave_ > 0 ? C_WHITE : C_GREY) : C_OFF);
    set(CC_DOWN, octave_ > -2 ? (octave_ < 0 ? C_WHITE : C_GREY) : C_OFF);
    set(CC_MENU, settings_ ? C_WHITE : C_GREY_DARK);
    set(CC_BACK, exit_prompt_ ? C_WHITE : C_GREY_DARK);
    set(CC_DELETE, menu ? C_RED : C_GREY_DARK);
    set(CC_COPY, menu ? C_GREEN : C_OFF);
    set(CC_CAPTURE, menu ? C_BLUE : C_OFF);
}

static inline bool put_pkt(uint8_t *spi, int *n, uint8_t cin_cable, uint8_t st,
                           uint8_t d1, uint8_t d2)
{
    if(*n >= SCHWUNG_MIDI_OUT_MAX)
        return false;
    uint8_t *p = spi + SCHWUNG_OFF_OUT_MIDI + (*n) * 4;
    p[0]       = cin_cable;
    p[1]       = st;
    p[2]       = d1;
    p[3]       = d2;
    (*n)++;
    return true;
}

void Surface::Tick(uint8_t *spi)
{
    tick_++;
    int n = 0;

    // MIDI out first (to USB-A, cable 2), then lights with what is left
    uint8_t m[3];
    while(n < 6 && eng_.PopMidiOut(m))
        put_pkt(spi, &n, (uint8_t)(0x20 | (m[0] >> 4)), m[0], m[1], m[2]);

    if((tick_ & 7) == 0)
        ComputeLeds();

    // one stale entry re-sent per tick keeps a dropped packet from sticking
    refresh_i_ = (refresh_i_ + 1) % (32 + 16 + 128);
    if(refresh_i_ < 32)
        sent_pad_[refresh_i_] = 0xFF;
    else if(refresh_i_ < 48)
        sent_step_[refresh_i_ - 32] = 0xFF;
    else
        sent_cc_[refresh_i_ - 48] = 0xFF;

    for(int p = 0; p < 32 && n < SCHWUNG_MIDI_OUT_MAX; p++)
        if(want_pad_[p] != sent_pad_[p]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + p), want_pad_[p]))
            sent_pad_[p] = want_pad_[p];
    for(int s = 0; s < 16 && n < SCHWUNG_MIDI_OUT_MAX; s++)
        if(want_step_[s] != sent_step_[s]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + s), want_step_[s]))
            sent_step_[s] = want_step_[s];
    for(int c = 0; c < 128 && n < SCHWUNG_MIDI_OUT_MAX; c++)
        if(cc_used_[c] && want_cc_[c] != sent_cc_[c]
           && put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)c, want_cc_[c]))
            sent_cc_[c] = want_cc_[c];

    if(disp_.AtFrameStart())
        Draw();
    disp_.PushSlice(spi);
}

int Surface::WriteAllOff(uint8_t *spi, int start)
{
    // returns where to continue, or -1 when every light has been sent
    int n = 0;
    int i = start;
    for(; i < 32 + 16 + 128 && n < SCHWUNG_MIDI_OUT_MAX; i++)
    {
        if(i < 32)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + i), 0);
        else if(i < 48)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + i - 32), 0);
        else if(cc_used_[i - 48])
            put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)(i - 48), 0);
    }
    return i >= 32 + 16 + 128 ? -1 : i;
}

/* ---- screen ----------------------------------------------------------------- */

void Surface::Draw()
{
    disp_.Clear();
    if(exit_prompt_ && eng_.NowMs() - exit_prompt_t_ > 3000)
        exit_prompt_ = false;
    if(exit_prompt_)
    {
        disp_.TextCentered(18, "EXIT MUNCHI TAPE?", 1);
        disp_.TextCentered(34, "BACK AGAIN TO EXIT", 1);
        disp_.TextCentered(46, "LOOPER + BUFFER ARE LOST", 1);
        return;
    }
    if(settings_)
        DrawSettings();
    else
        DrawMain();
}

void Surface::DrawSettings()
{
    disp_.Text(0, 0, "SETTINGS");
    disp_.HLine(0, 9, 128);
    const Options &o = eng_.Opts();
    static const char *mons[3] = {"DRY", "THRU FX", "SEND/RET"};
    static const char *srcs[3] = {"MIC", "LINE", "RESAMPLE"};
    int first = settings_cursor_ - 4 < 0 ? 0 : settings_cursor_ - 4;
    for(int row = 0; row < 5; row++)
    {
        int i = first + row;
        if(i > kNumSettings - 1)
            break;
        char v[16] = "";
        switch(i)
        {
            case 0: strcpy(v, srcs[(int)eng_.Eng().GetInputSource() % 3]); break;
            case 1: strcpy(v, o.record_latch ? "ON" : "OFF"); break;
            case 2: strcpy(v, o.tape_slew ? "ON" : "OFF"); break;
            case 3: strcpy(v, mons[o.monitor_pos % 3]); break;
            case 4: strcpy(v, o.split_delay ? "ON" : "OFF"); break;
            case 5: strcpy(v, o.ps_quant ? "ON" : "OFF"); break;
            case 6: strcpy(v, eng_.key_velocity < 0.f ? "ON" : "OFF"); break;
            case 7: snprintf(v, sizeof(v), "%d", o.midi_ch_in + 1); break;
            case 8: snprintf(v, sizeof(v), "%d", o.midi_ch_out + 1); break;
        }
        int y = 12 + row * 10;
        disp_.Text(2, y, kSettingNames[i]);
        disp_.TextRight(126, y, v);
        if(i == settings_cursor_)
            disp_.Invert(0, y - 1, 128, 9);
    }
}

void Surface::DrawSampleBar(int y)
{
    daisy::Engine &x = eng_.Eng();
    float s = x.GetStartFrac(), e = x.GetEndFrac();
    disp_.Frame(0, y, 128, 7);
    int xs = 1 + (int)(s * 125), xe = 1 + (int)(e * 125);
    if(xe > xs)
        disp_.Rect(xs, y + 2, xe - xs, 3);
    if(x.GetLatestVoicePlaying())
    {
        int px = 1 + (int)(x.GetVoicePosition() * 125);
        disp_.Rect(px, y - 1, 1, 9, 1);
        disp_.Pixel(px, y + 3, 0);
    }
}

void Surface::DrawLooper(int y)
{
    daisy::Engine &x = eng_.Eng();
    const char    *state;
    if(x.IsLooperRecordArmed())
        state = "ARMED";
    else if(x.GetLooperIsEmpty())
        state = "LOOP -";
    else if(x.IsLooperFirstRecording() && x.IsLooperRecording())
        state = "REC";
    else if(x.IsLooperRecording())
        state = "DUB";
    else if(x.IsLooperPlaying())
        state = "PLAY";
    else
        state = "PAUSE";
    disp_.Text(0, y, state);
    if(!x.GetLooperIsEmpty() || x.IsLooperRecording())
    {
        disp_.Frame(40, y, 88, 7);
        int w = (int)(x.GetLooperPosition() * 86);
        if(w > 0)
            disp_.Rect(41, y + 1, w, 5);
    }
}

void Surface::DrawMain()
{
    daisy::Engine &x = eng_.Eng();
    char           buf[40];

    // header: mode, bank, slot | octave, input, record switch
    bool keys = x.GetVoiceMode() == VoiceMode::JAMMI;
    if(keys)
    {
        int n;
        if(x.GetVoiceSlot() == 15)
            n = snprintf(buf, sizeof(buf), "KEYS BUF");
        else
            n = snprintf(buf, sizeof(buf), "KEYS %c%d", 'A' + (int)x.GetVoiceBank(),
                         (int)x.GetVoiceSlot());
        // Keys mode can browse a bank without leaving the playing slot
        // (the firmware's behaviour); say which bank the pads/steps show.
        if((size_t)x.GetBank() != x.GetVoiceBank() || x.GetVoiceSlot() == 15)
            snprintf(buf + n, sizeof(buf) - n, " [%c]", 'A' + x.GetBank());
    }
    else
        snprintf(buf, sizeof(buf), "KIT %c", 'A' + x.GetBank());
    disp_.Text(0, 0, buf);
    static const char *srcs[3] = {"MIC", "LINE", "RSMP"};
    snprintf(buf, sizeof(buf), "%s%s", eng_.GetSwitch() ? "" : "REC ",
             srcs[(int)x.GetInputSource() % 3]);
    disp_.TextRight(128, 0, buf);
    if(octave_)
    {
        snprintf(buf, sizeof(buf), "%+d", octave_);
        disp_.Text(64, 0, buf);
    }

    DrawSampleBar(11);

    const uint32_t now = eng_.NowMs();
    int            y   = 24;
    if(eng_.CardSeeding())
        disp_.Text(0, 54, "FIRST RUN: COPYING CARD");
    else if(!eng_.CardReady())
    {
        snprintf(buf, sizeof(buf), "LOADING %d/%d", eng_.CardLoaded(), eng_.CardTotal());
        disp_.Text(0, 54, buf);
    }

    if(x.Recording())
    {
        if((now / 400) & 1)
            disp_.Rect(0, y + 2, 6, 6);
        snprintf(buf, sizeof(buf), "%.1fs",
                 (double)x.GetChompiLength() / 2.0 / 48000.0);
        disp_.Text(10, y, "RECORDING", 1);
        disp_.Text(10, y + 10, buf, 1);
    }
    else if(eng_.MenuActive())
    {
        const char *l1 = "SHIFT", *l2 = "PADS: MODE BANK IN", *l3 = "WHITE KEYS: SLOT";
        switch(eng_.PresetMode())
        {
            case MunchiEngine::PM_ERASE_SEL:
                l1 = "ERASE";
                l2 = "PICK A SLOT";
                l3 = "SHIFT TO CONFIRM";
                break;
            case MunchiEngine::PM_COPY_SRC:
                l1 = "COPY";
                l2 = "PICK SOURCE";
                l3 = "LOOP/PLAY = LOOPER";
                break;
            case MunchiEngine::PM_COPY_DEST:
                l1 = "COPY";
                l2 = "PICK DESTINATION";
                l3 = "SHIFT TO CONFIRM";
                break;
            case MunchiEngine::PM_SAVE_SEL:
                l1 = "SAVE BUFFER";
                l2 = "PICK A SLOT";
                l3 = "SHIFT TO CONFIRM";
                break;
            case MunchiEngine::PM_SAVING:
            case MunchiEngine::PM_COPYING:
            case MunchiEngine::PM_ERASING:
                l1 = "WORKING";
                l2 = "";
                l3 = "";
                break;
        }
        disp_.Text(0, y, l1);
        disp_.Text(0, y + 10, l2);
        disp_.Text(0, y + 20, l3);
    }
    else if(show_t_ && now - show_t_ < 1500)
    {
        disp_.Text(0, y, show_name_);
        disp_.Text(0, y + 11, show_value_, 2);
    }
    else
    {
        // at rest: the eight knobs, as the hardware's LEDs would show them
        static const char *kn[2][8] = {
            {"SPD", "STA", "END", "SPC", "LOF", "FLT", "LOP", "IN"},
            {"GAN", "ATK", "DEC", "SPC", "LOF", "FLT", "LOP", "IN"}};
        float vals[8] = {page_ ? eng_.EncValue(1, 0) : eng_.EncValue(0, 0),
                         page_ ? eng_.EncValue(1, 1) : eng_.EncValue(0, 1),
                         page_ ? eng_.EncValue(1, 2) : eng_.EncValue(0, 2),
                         eng_.EncValue(0, 3),
                         eng_.EncValue(1, 3),
                         eng_.EncValue(2, 3),
                         eng_.EncValue(0, 4),
                         eng_.EncValue(1, 5)};
        for(int i = 0; i < 8; i++)
        {
            int cx = (i % 4) * 32, cy = y + (i / 4) * 13;
            disp_.Text(cx, cy, kn[page_][i]);
            disp_.Frame(cx, cy + 8, 28, 3);
            disp_.HLine(cx, cy + 9, 1 + (int)(vals[i] * 27));
        }
    }

    if(eng_.CardReady())
        DrawLooper(56);
}

} // namespace munchi
