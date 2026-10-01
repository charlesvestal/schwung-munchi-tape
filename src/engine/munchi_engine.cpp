/* munchi_engine.cpp -- see munchi_engine.h.
 *
 * The control code below is NormalPage.h, MenuPage.h and ui.h from CHOMPI
 * TAPE 2.0, ported with their structure intact so it can be read against the
 * original. LED drawing is not here (surface.cpp owns the Move's lights);
 * everything a page's Draw() did to the ENGINE is, in UiTick()/ApplyKnob().
 */
#include "munchi_engine.h"
#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <chrono>
#include <algorithm>

uint32_t daisy::System::now_ms = 0;

namespace munchi
{
using namespace daisy;

/* ---- tables from the firmware ------------------------------------------ */

// NormalPage.h cc_map
static const uint8_t cc_map[3][6] = {{20, 21, 22, 23, 24, 25},
                                     {28, 29, 30, 31, 0, 32},
                                     {0, 0, 0, 33, 0, 0}};

// NormalPage.h key_map (MIDI note / CC per SwId)
static const uint8_t key_map[40] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x15, 0x00, 0x31, 0x32, 0x34,
    0x35, 0x37, 0x33, 0x36, 0x38, 0x30, 0x39, 0x3b, 0x3c, 0x3e,
    0x40, 0x3a, 0x3d, 0x3f, 0x41, 0x43, 0x45, 0x47, 0x48, 0x42,
    0x44, 0x46, 0x05, 0x17, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ui.h enc_defaults
static const float enc_defaults[3][6] = {
    {.83f, 0.f, 1.f, 0.f, .75f, .84f},
    {.704f, 0.f, 0.f, 0.f, 0.f, .75f},
    {0.f, 0.f, 0.f, .5f, 0.f, 0.f},
};

// ui.h midi2key: MIDI note - 24 -> SwId
static const uint8_t midi2key[49] = {
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 15, 7,  8,  12, 9,  10, 13, 11, 14, 16,
    21, 17, 18, 22, 19, 23, 20, 24, 29, 25, 30, 26, 31, 27, 28};

// NormalPage.h
static const float kEncoderFineStep   = .003f;
static const float kEncoderCoarseStep = .01f;

// pitch, start, end, att, decay, autoloop, sustainactive, gain, pan
static const float preset_defaults[9] = {enc_defaults[0][0], enc_defaults[0][1],
                                         enc_defaults[0][2], enc_defaults[1][1],
                                         enc_defaults[1][2], 1.f,
                                         1.f, enc_defaults[1][0], .5f};

int KeyIdToNote(int id)
{
    if(id < KEY_16 || id > KEY_25 || id == ENC_6_SW)
        return -1;
    return key_map[id];
}

int NoteToKeyId(int note)
{
    int k = note - 24;
    if(k < 24 || k > 48) // 48..72: the 25 keys
        return -1;
    return midi2key[k];
}

/* ---- Presets (PresetManager.h) ----------------------------------------- */

void Presets::Init(const float *defaults)
{
    updated = true;
    for(int m = 0; m < kModes; m++)
        for(int b = 0; b < kBanks; b++)
            for(int s = 0; s < kSlots; s++)
            {
                valid[m][b][s] = false;
                for(int c = 0; c < kControls; c++)
                    values[m][b][s][c] = defaults[c];
            }
    for(int c = 0; c < kControls; c++)
        chompi_value[c] = defaults[c];
    chompi_valid = false;
}

bool Presets::IsValid(size_t mode, size_t bank, size_t slot)
{
    slot -= 1;
    if(slot > kSlots || mode >= kModes || bank >= kBanks)
        return false;
    if(slot == 14)
        return chompi_valid;
    return valid[mode][bank][slot];
}

float Presets::GetValue(size_t mode, size_t bank, size_t slot, size_t control)
{
    slot -= 1;
    if(slot > kSlots || mode >= kModes || bank >= kBanks || control >= kControls)
        return 0xff;
    if(slot == 14)
        return chompi_valid ? chompi_value[control] : 0xff;
    if(!valid[mode][bank][slot])
        return 0xff;
    return values[mode][bank][slot][control];
}

void Presets::SetValue(float value, size_t mode, size_t bank, size_t slot,
                       size_t control)
{
    slot -= 1;
    if(slot > kSlots || mode >= kModes || bank >= kBanks || control >= kControls)
        return;
    if(slot == 14)
    {
        chompi_value[control] = value;
        chompi_valid          = true;
    }
    else
    {
        updated                      = true;
        values[mode][bank][slot][control] = value;
        valid[mode][bank][slot]      = true;
    }
}

void Presets::Invalidate(uint8_t mode, uint8_t bank, uint8_t slot)
{
    slot -= 1;
    if(slot >= kSlots || mode >= kModes || bank >= kBanks)
        return;
    valid[mode][bank][slot] = false;
    updated                 = true;
}

void Presets::Save(uint8_t mode, uint8_t bank, uint8_t slot)
{
    slot -= 1;
    if(slot >= kSlots || mode >= kModes || bank >= kBanks)
        return;
    for(int i = 0; i < kControls; i++)
        values[mode][bank][slot][i] = chompi_value[i];
    valid[mode][bank][slot] = chompi_valid;
    updated                 = true;
}

void Presets::Copy(uint8_t ms, uint8_t bs, uint8_t ss, uint8_t mt, uint8_t bt,
                   uint8_t st)
{
    st -= 1;
    ss -= 1;
    if(ss > kSlots || ms >= kModes || bs >= kBanks)
        return;
    if(st > kSlots || mt >= kModes || bt >= kBanks)
        return;
    float *src  = ss == 14 ? chompi_value : values[ms][bs][ss];
    float *dest = st == 14 ? chompi_value : values[mt][bt][st];
    for(int i = 0; i < kControls; i++)
        dest[i] = src[i];
    bool v = ss == 14 ? chompi_valid : valid[ms][bs][ss];
    if(st == 14)
        chompi_valid = v;
    else
        valid[mt][bt][st] = v;
    updated = true;
}

/* presets.json: [[[[c0..c8,valid] x14] x5] x2, 2] with controls x1000.
 * A v1 file has 7 controls and no trailing version. */
bool Presets::Parse(const char *json)
{
    // Flatten every scalar in document order, tracking nesting depth.
    struct Tok
    {
        int   depth;
        bool  is_bool;
        float num;
    };
    std::vector<Tok> toks;
    int              depth = 0;
    for(const char *p = json; *p; p++)
    {
        if(*p == '[')
            depth++;
        else if(*p == ']')
            depth--;
        else if(*p == 't' || *p == 'f')
        {
            toks.push_back({depth, true, *p == 't' ? 1.f : 0.f});
            while(*p && *p != ',' && *p != ']')
                p++;
            p--;
        }
        else if(*p == '-' || (*p >= '0' && *p <= '9'))
        {
            char *e;
            float v = strtof(p, &e);
            toks.push_back({depth, false, v});
            p = e - 1;
        }
    }
    // v2 has the version number at depth 1 after the arrays
    int ctrl = (toks.size() && toks.back().depth == 1) ? kControls : 7;
    size_t idx  = 0;
    for(int m = 0; m < kModes; m++)
        for(int b = 0; b < kBanks; b++)
            for(int s = 0; s < kSlots; s++)
            {
                if(idx + ctrl + 1 > toks.size())
                    return false;
                bool v = toks[idx + ctrl].is_bool && toks[idx + ctrl].num > 0.f;
                if(v)
                    for(int c = 0; c < ctrl; c++)
                        values[m][b][s][c] = .001f * toks[idx + c].num;
                valid[m][b][s] = v;
                idx += ctrl + 1;
            }
    updated = true;
    return true;
}

std::string Presets::Serialize() const
{
    std::string out = "[";
    char        tmp[32];
    for(int m = 0; m < kModes; m++)
    {
        out += "[";
        for(int b = 0; b < kBanks; b++)
        {
            out += "[";
            for(int s = 0; s < kSlots; s++)
            {
                out += "[";
                for(int c = 0; c < kControls; c++)
                {
                    snprintf(tmp, sizeof(tmp), "%d,", int(values[m][b][s][c] * 1000));
                    out += tmp;
                }
                out += valid[m][b][s] ? "true]" : "false]";
                if(s < kSlots - 1)
                    out += ",";
            }
            out += "]";
            if(b < kBanks - 1)
                out += ",";
        }
        out += "],";
    }
    out += "2]";
    return out;
}

/* ---- engine lifecycle -------------------------------------------------- */

MunchiEngine::MunchiEngine() {}

MunchiEngine::~MunchiEngine()
{
    StopCardWorker();
    WorkerFreeRetired(true);
    for(int m = 0; m < kModes; m++)
        for(int b = 0; b < kBanks; b++)
            for(int s = 1; s <= kSlots; s++)
            {
                Sample *p = store_.Publish(m, b, s, nullptr);
                if(p)
                {
                    free(p->mem.mem);
                    delete p;
                }
            }
    delete reverb_;
    free(del_mem_);
    free(loop_buff_.mem);
    free(chompi_buff_.mem);
}

int MunchiEngine::Init()
{
    reverb_  = new daisysp::Reverb();
    del_mem_ = (chompi::InterpolatedDelayLine::AudioSample *)calloc(
        kMaxDelayTime, sizeof(chompi::InterpolatedDelayLine::AudioSample));
    loop_buff_.mem   = (int16_t *)calloc(kMaxRamBuffSize, sizeof(int16_t));
    chompi_buff_.mem = (int16_t *)calloc(kMaxRamBuffSize, sizeof(int16_t));
    if(!reverb_ || !del_mem_ || !loop_buff_.mem || !chompi_buff_.mem)
        return -1;
    loop_buff_.length = chompi_buff_.length = 0;

    daisy::System::now_ms = 0;
    presets_.Init(preset_defaults);
    // Nothing to save until the card's own presets.json has been read: a
    // write before then would replace the user's file with the defaults.
    presets_.updated = false;

    // ui.h Init -> NormalPage::Init
    for(int knob = 0; knob < 6; knob++)
        for(int page = 0; page < 3; page++)
            enc_values_[page][knob] = enc_defaults[page][knob];
    if(opts_.split_delay)
        enc_values_[0][3] = .5f;
    quantized_pitch_normal_ = !opts_.ps_quant;

    engine_.Init(48000.f, reverb_, del_mem_, &loop_buff_, &chompi_buff_,
                 opts_.record_latch, opts_.tape_slew,
                 MonitorMode(opts_.monitor_pos), &store_);

    engine_.SetGlobalPitch(1.f);
    engine_.SetReverse(false);
    engine_.SetInputGain(.75f);

    // MenuPage::Init
    final_comp_ = 0.f;
    delay_time_ = .5f;
    resonance_  = 0.f;
    warble_     = 0.f;
    engine_.SetFinalComp(final_comp_);
    engine_.SetFilterResonance(resonance_);
    engine_.SetDelayTime(delay_time_);
    engine_.SetWarble(warble_);

    // chompi_main: default buffer contents
    engine_.FillDefaultSample(48000.f);

    for(int k = 0; k < 6; k++)
        ApplyKnob(k, k == 3 ? 0 : knob_page_[k]);
    ApplyKnob(3, 1);
    ApplyKnob(3, 2);
    ApplyKnob(5, 1);

    up_.Init(44100.0, 48000.0);
    down_.Init(48000.0, 44100.0);
    // a few ms of silence so the output side never runs dry
    out44_count_ = 160;
    memset(out44_l_, 0, sizeof(out44_l_));
    memset(out44_r_, 0, sizeof(out44_r_));

    init_time_ = daisy::System::now_ms;
    return 0;
}

void MunchiEngine::OptionsChanged()
{
    quantized_pitch_normal_ = !opts_.ps_quant;
    engine_.SetRecordLatch(opts_.record_latch);
    engine_.SetMonitorMode(MonitorMode(opts_.monitor_pos));
    engine_.SetTapeSlew(opts_.tape_slew);
    ApplyKnob(3, 0);
    SaveOptions();
}

/* ---- audio ------------------------------------------------------------- */

void MunchiEngine::ProcessHostBlock(const int16_t *in, int16_t *out, int frames)
{
    for(int i = 0; i < frames; i++)
    {
        up_.Push(in[i * 2] * (1.f / 32768.f), in[i * 2 + 1] * (1.f / 32768.f));
        while(up_.CanPull())
        {
            float l, r;
            up_.Pull(&l, &r);
            // CHOMPI inputs: 0 = mic, 1 = unused, 2/3 = aux L/R. Move has one
            // stereo input that is the mic until a cable is in; both feeds
            // carry it and the engine's input source picks which it reads.
            // Move's internal mic arrives on the left channel only; a sum
            // keeps it whole (and centred) whichever side carries it.
            in48_[0][in48_count_] = l + r;
            in48_[1][in48_count_] = 0.f;
            in48_[2][in48_count_] = l;
            in48_[3][in48_count_] = r;
            if(++in48_count_ == kEngineBlock)
            {
                RunEngineBlock();
                in48_count_ = 0;
            }
        }
    }

    int n = frames < out44_count_ ? frames : out44_count_;
    for(int i = 0; i < n; i++)
    {
        out[i * 2]     = (int16_t)f2s16(out44_l_[i]);
        out[i * 2 + 1] = (int16_t)f2s16(out44_r_[i]);
    }
    for(int i = n; i < frames; i++)
        out[i * 2] = out[i * 2 + 1] = 0;
    memmove(out44_l_, out44_l_ + n, (out44_count_ - n) * sizeof(float));
    memmove(out44_r_, out44_r_ + n, (out44_count_ - n) * sizeof(float));
    out44_count_ -= n;
}

void MunchiEngine::RunEngineBlock()
{
    // AudioCallback(): controls, then engine.Prepare(), then Process()
    UiTick();
    engine_.Prepare();

    const float *ins[4] = {in48_[0], in48_[1], in48_[2], in48_[3]};
    float       *outs[4] = {out48_[0], out48_[1], out48_[2], out48_[3]};
    engine_.Process(ins, outs, kEngineBlock);

    // The headphone bus: the line mix plus the dry monitor in HP mode.
    // Brought up to line level (kLineOutGain / kHpGain) since Move has one
    // output and it should be as loud as CHOMPI's main out.
    const float kHpToLine = kLineOutGain / kHpGain;
    for(int i = 0; i < kEngineBlock; i++)
    {
        down_.Push(out48_[0][i] * kHpToLine, out48_[1][i] * kHpToLine);
        while(down_.CanPull() && out44_count_ < 1024)
        {
            down_.Pull(&out44_l_[out44_count_], &out44_r_[out44_count_]);
            out44_count_++;
        }
    }

    // 48 samples at 48 kHz: the engine clock advances exactly 1 ms
    daisy::System::now_ms++;
    store_.engine_blocks.fetch_add(1, std::memory_order_release);
}

/* What ui.h GenerateEvents() and the pages' Draw() did to the engine, run
 * once per engine block (1 ms, the firmware's UI rate). */
void MunchiEngine::UiTick()
{
    const uint32_t now = daisy::System::now_ms;

    if(init_ignore_ && now - init_time_ > 1500)
        init_ignore_ = false;

    // NormalPage::Draw
    if(engine_.CheckReset())
    {
        enc_values_[0][4] = enc_defaults[0][4];
        engine_.ResetLooperPitchQuant();
    }
    engine_.SetInputMonitor(!switch_state_);

    if((now & 15) == 0) // the LED canvas refreshed every 16 ms
    {
        for(int k = 0; k < 3; k++)
            if(knob_page_[k] == 1)
                ApplyKnob(k, 1);
    }

    // GenerateEvents: close the menu page when it says it can close
    if(menu_active_ && MenuIsClosable())
        menu_active_ = false;

    // presets.json arrived from the worker
    if(Presets *p = loaded_presets_.exchange(nullptr, std::memory_order_acq_rel))
    {
        presets_ = *p;
        adopted_presets_ = p; // freed by the worker, never here
    }

    // FileCopier finished
    if(copy_done_.load(std::memory_order_acquire))
    {
        copy_done_.store(false);
        CopyFinished();
    }

    // SDCallback: presets were written whenever no voice was playing
    if(now - presets_check_t_ > 1000)
    {
        presets_check_t_ = now;
        if(presets_.updated && !engine_.AnyVoicesPlaying()
           && !presets_ready_.load() && worker_run_.load()
           && presets_known_.load(std::memory_order_acquire)
           && !loaded_presets_.load())
        {
            presets_snapshot_ = presets_.Serialize();
            presets_.updated  = false;
            presets_ready_.store(true, std::memory_order_release);
        }
    }

    // tell the worker what to load first
    want_mode_.store(int(engine_.GetVoiceMode()));
    want_bank_.store(engine_.GetBank());
}

/* ---- NormalPage -------------------------------------------------------- */

float MunchiEngine::PitchFromEnc(float val)
{
    val       = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1
    float inv = val < 0.f ? -1.f : 1.f;
    float pitch;
    if(fabsf(val) < .33f) // .01x - .5x
        pitch = val * 1.484848f + .01f * inv;
    else if(fabsf(val) < .66f) // .5x - 1x
        pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
    else // 1x - 2x
        pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;
    return pitch;
}

/* The fx_->Set*() calls in NormalPage::Draw's per-encoder switch. */
void MunchiEngine::ApplyKnob(int knob, int page)
{
    float value = enc_values_[page][knob];
    switch(knob)
    {
        case 0:
            if(page == 1)
                engine_.SetGain(value);
            break;
        case 1:
            if(page == 1)
                engine_.SetAttack(value);
            break;
        case 2:
            if(page == 1)
                engine_.SetDecay(value);
            break;
        case 3:
            if(page == 0)
            {
                if(opts_.split_delay)
                {
                    if(value < .5f)
                    {
                        engine_.SetReverb(0.f);
                        engine_.SetDelayFeedback((.5f - value) * 2.f);
                    }
                    else
                    {
                        engine_.SetReverb((value - .5f) * 2.f);
                        engine_.SetDelayFeedback(0.f);
                    }
                }
                else
                {
                    engine_.SetReverb(value);
                    engine_.SetDelayFeedback(value);
                }
            }
            else if(page == 1)
                engine_.SetSaturate(value);
            else
                engine_.SetFilter(value);
            break;
        case 5:
            if(page == 0)
                engine_.SetMainGain(value);
            else
                engine_.SetInputGain(value);
            break;
    }
}

void MunchiEngine::Button(int id, bool rising)
{
    // ui.h: the CHOMPI key opens the menu page in play mode, on the press
    if(id == KEY_26 && rising && switch_state_ && !menu_active_)
        OpenMenu();

    if(menu_active_)
    {
        if(MenuButton(id, rising))
            return; // handled; otherwise it falls through to NormalPage
    }
    NormalButton(id, rising);
}

void MunchiEngine::OpenMenu()
{
    if(menu_active_)
        return;
    menu_active_ = true;
    MenuFocusGained();
}

void MunchiEngine::NormalButton(int id, bool rising)
{
    if(init_ignore_ || Copying())
        return;

    switch(id)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5: break;

        // encoder clicks toggle pages (surface.cpp owns the pages on Move)
        case ENC_1_SW:
        case ENC_2_SW:
        case ENC_3_SW:
        case ENC_4_SW: break;

        case ENC_6_SW: break;

        // reset the looper pitch
        case ENC_5_SW:
            if(!rising)
            {
                enc_values_[0][4] = enc_defaults[0][4];
                SendCC(cc_map[0][4], (int)(enc_values_[0][4] * 127.f));
            }
            engine_.SetLooperPitch(1.f);
            engine_.ResetLooperPitchQuant();
            break;

        case SW_TOG: break;

        case KEY_27: // play
            engine_.LooperPlayButton(rising);
            SendCC(26, rising ? 127 : 0);
            break;

        case KEY_28: // loop
            if(!engine_.Recording())
                engine_.LooperRecordButton(rising);
            SendCC(27, rising ? 127 : 0);
            break;

        case KEY_26:
        {
            chompi_key_pressed_ = rising;
            if(!switch_state_)
                SendCC(key_map[id], rising ? 127 : 0);
            else
                midi_channel_ = rising;

            // chompi mode
            if(!switch_state_)
            {
                bool latch = engine_.GetRecordLatch();
                bool rec   = engine_.Recording();
                if(rising && !rec)
                    engine_.StartNewRecording(0);
                else if(!rising && !latch && rec)
                    StopVoiceRecording();
                else if(rising && rec && latch)
                    StopVoiceRecording();
            }
            break;
        }

        // keys
        default:
            if(KeyIdToNote(id) < 0)
                break;
            if(rising)
            {
                if(engine_.GetVoiceMode() == VoiceMode::CUBBI)
                {
                    size_t slot = KeyToSlot(id);
                    if(slot == kSlotNone)
                        return;
                    OpenCubbiSlot(slot);
                }

                engine_.request_fifo.PushBack(KeyRequest(
                    KeyRequest::Type::START,
                    key_map[id] - 60 + key_transpose, id,
                    pad_velocity_ > 0.f ? pad_velocity_ : 127.f));

                if(engine_.GetLooperRecordArm())
                    engine_.ToggleLooperRecord();

                SendNoteOn(key_map[id], 127);
            }
            else
            {
                engine_.request_fifo.PushBack(
                    KeyRequest(KeyRequest::Type::STOP, 0, id, 127.f));
                SendNoteOff(key_map[id]);
            }
            break;
    }
}

void MunchiEngine::OpenCubbiSlot(size_t slot)
{
    if(!engine_.GetFileExists(slot - 1))
        return;

    size_t mode = static_cast<size_t>(engine_.GetVoiceMode());
    size_t bank = engine_.GetBank();

    bool  loop    = true;
    bool  sustain = true;
    float pan     = .5f;

    if(!presets_.IsValid(mode, bank, slot))
    {
        enc_values_[0][0] = enc_defaults[0][0];
        enc_values_[0][1] = enc_defaults[0][1];
        enc_values_[0][2] = enc_defaults[0][2];
        enc_values_[1][0] = enc_defaults[1][0];
        enc_values_[1][1] = enc_defaults[1][1];
        enc_values_[1][2] = enc_defaults[1][2];
    }
    else
    {
        enc_values_[0][0] = presets_.GetValue(mode, bank, slot, 0);
        enc_values_[0][1] = presets_.GetValue(mode, bank, slot, 1);
        enc_values_[0][2] = presets_.GetValue(mode, bank, slot, 2);
        enc_values_[1][1] = presets_.GetValue(mode, bank, slot, 3);
        enc_values_[1][2] = presets_.GetValue(mode, bank, slot, 4);
        loop              = presets_.GetValue(mode, bank, slot, 5);
        sustain           = presets_.GetValue(mode, bank, slot, 6);
        enc_values_[1][0] = presets_.GetValue(mode, bank, slot, 7);
        pan               = presets_.GetValue(mode, bank, slot, 8);
    }

    float pitch = PitchFromEnc(enc_values_[0][0]);
    engine_.OpenCubbiSlot(pitch, enc_values_[0][1], enc_values_[0][2],
                          enc_values_[1][1], enc_values_[1][2], loop, sustain,
                          enc_values_[1][0], pan);
}

void MunchiEngine::Encoder(int knob, int page, int turns, bool menu)
{
    if(knob < 0 || knob > 5 || turns == 0)
        return;
    knob_page_[knob] = page;
    // ui.h GenerateEvents: the pitch encoders step 1, the others 3
    int t = (knob == 0 || knob == 4) ? turns : turns * 3;
    if(menu)
        MenuEncoder(knob, t);
    else
        NormalEncoder(knob, t, 0);
}

void MunchiEngine::NormalEncoder(int encoderID, int turns, int stepsPerRevolution)
{
    if(init_ignore_ || Copying())
        return;

    int   page    = knob_page_[encoderID];
    float old_val = enc_values_[page][encoderID];
    bool  update_presets = true;

    if(stepsPerRevolution > 0)
    {
        enc_values_[page][encoderID] = turns / 127.f;
    }
    else
    {
        float inc = turns * kEncoderCoarseStep;
        if((encoderID == 0 && page == 0 && quantized_pitch_normal_)
           || (encoderID == 4 && quantized_pitch_normal_))
            inc = 0.f;
        else if((encoderID == 0 && page == 0 && !quantized_pitch_normal_)
                || (encoderID == 1 && page == 0) || (encoderID == 2 && page == 0)
                || (encoderID == 4 && !quantized_pitch_normal_))
            inc = turns * kEncoderFineStep;
        enc_values_[page][encoderID] += inc;
    }

    enc_values_[page][encoderID]
        = daisysp::fclamp(enc_values_[page][encoderID], 0.f, 1.f);

    if(encoderID == 0 && page == 0)
    {
        if(quantized_pitch_normal_)
            enc_values_[0][0] = engine_.SetGlobalPitchQuantized(turns, enc_values_[0][0]);
        else
            engine_.SetGlobalPitchFree(enc_values_[0][0]);
    }
    else if(encoderID == 4)
    {
        if(engine_.IsLooperPlaying())
        {
            if(quantized_pitch_normal_)
                enc_values_[0][4]
                    = engine_.SetLooperPitchQuantized(turns, enc_values_[0][4]);
            else
                engine_.SetLooperPitchFree(enc_values_[0][4]);
        }
        else
        {
            enc_values_[0][4] = old_val;
            engine_.SetLooperScrub(turns);
        }
    }

    // don't allow end point too close to start point
    if(page == 0 && (encoderID == 1 || encoderID == 2))
    {
        if((enc_values_[0][1] + .01f) >= enc_values_[0][2])
        {
            enc_values_[page][encoderID] = old_val;
        }
        else if(encoderID == 1)
        {
            if(!engine_.SetStartPoint(enc_values_[0][1]) && turns > 0)
            {
                update_presets               = false;
                enc_values_[page][encoderID] = old_val;
            }
        }
        else if(encoderID == 2)
        {
            if(!engine_.SetEndPoint(enc_values_[0][2]) && turns < 0)
            {
                update_presets               = false;
                enc_values_[page][encoderID] = old_val;
            }
        }
    }

    if(stepsPerRevolution == 0 && cc_map[page][encoderID])
        SendCC(cc_map[page][encoderID], (int)(enc_values_[page][encoderID] * 127));

    ApplyKnob(encoderID, page);

    if(encoderID < 3 && update_presets)
        DumpValuePresets();
}

void MunchiEngine::DumpValuePresets()
{
    size_t mode = static_cast<size_t>(engine_.GetVoiceMode());
    size_t bank = engine_.GetBank();
    size_t slot = engine_.GetVoiceSlot();

    presets_.SetValue(enc_values_[0][0], mode, bank, slot, 0);
    presets_.SetValue(enc_values_[0][1], mode, bank, slot, 1);
    presets_.SetValue(enc_values_[0][2], mode, bank, slot, 2);
    presets_.SetValue(enc_values_[1][1], mode, bank, slot, 3);
    presets_.SetValue(enc_values_[1][2], mode, bank, slot, 4);
    presets_.SetValue(engine_.GetAutoLoop(), mode, bank, slot, 5);
    presets_.SetValue(engine_.GetSustainActive(), mode, bank, slot, 6);
    presets_.SetValue(enc_values_[1][0], mode, bank, slot, 7);
    presets_.SetValue(engine_.GetPan(), mode, bank, slot, 8);
}

void MunchiEngine::SetSwitch(bool state)
{
    if(state && !switch_state_ && engine_.Recording() && !Copying())
        StopVoiceRecording();
    switch_state_ = state;
}

void MunchiEngine::StopVoiceRecording()
{
    enc_values_[0][0] = enc_defaults[0][0];
    enc_values_[0][1] = enc_defaults[0][1];
    enc_values_[0][2] = enc_defaults[0][2];
    enc_values_[1][0] = enc_defaults[1][0];
    enc_values_[1][1] = enc_defaults[1][1];
    enc_values_[1][2] = enc_defaults[1][2];
    engine_.StopRecording();
}

void MunchiEngine::SetLineIn(bool plugged)
{
    // chompi_main AudioCallback: jack detect picks the input
    engine_.SetInputSource(plugged ? InputSource::LINE_IN : InputSource::MIC);
}

/* ---- Munchi entry points ------------------------------------------------ */

void MunchiEngine::MenuKey(bool rising)
{
    // ui.h opened the menu page on the press and then delivered that same
    // press to it; the menu's own handler never falls through to the
    // NormalPage record branch, which is RecordKey's job here.
    if(rising && !menu_active_)
        OpenMenu();
    if(menu_active_)
        MenuButton(KEY_26, rising);
}

void MunchiEngine::RecordKey(bool rising)
{
    // NormalPage KEY_26 with the switch in record mode
    if(init_ignore_ || Copying())
        return;
    chompi_key_pressed_ = rising;
    SendCC(key_map[KEY_26], rising ? 127 : 0);
    bool latch = engine_.GetRecordLatch();
    bool rec   = engine_.Recording();
    if(rising && !rec)
        engine_.StartNewRecording(0);
    else if(!rising && !latch && rec)
        StopVoiceRecording();
    else if(rising && rec && latch)
        StopVoiceRecording();
}

void MunchiEngine::Click(int sw)
{
    if(sw == ENC_5_SW)
    {
        NormalButton(ENC_5_SW, true);
        NormalButton(ENC_5_SW, false);
        return;
    }
    MenuButton(sw, true);
    MenuButton(sw, false);
}

void MunchiEngine::SelectSlot(int slot)
{
    if(Copying() || engine_.GetVoiceMode() != VoiceMode::JAMMI)
        return;
    if(slot < 1 || slot > 15 || !engine_.GetFileExists(slot - 1))
        return;
    MenuSetVoiceSlot(slot);
}

void MunchiEngine::ClearLooper()
{
    engine_.ClearLooper();
    enc_values_[0][4] = enc_defaults[0][4];
    engine_.ResetLooperPitchQuant();
}

/* options.json, TAPE's format:
 *   {"chompi":[{"name":"Record Latch","value":false}, ...]} */
static bool json_find_value(const std::string &s, const char *name, std::string *out)
{
    std::string key = std::string("\"") + name + "\"";
    size_t      p   = s.find(key);
    if(p == std::string::npos)
        return false;
    p = s.find("\"value\"", p);
    if(p == std::string::npos)
        return false;
    p = s.find(':', p);
    if(p == std::string::npos)
        return false;
    p++;
    while(p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
        p++;
    size_t e = p;
    while(e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != '\n')
        e++;
    *out = s.substr(p, e - p);
    while(!out->empty() && (out->back() == ' ' || out->back() == '\r'))
        out->pop_back();
    return true;
}

void MunchiEngine::LoadOptions(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if(!f)
        return;
    std::string s;
    char        buf[1024];
    size_t      n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        s.append(buf, n);
    fclose(f);
    std::string v;
    if(json_find_value(s, "Record Latch", &v))
        opts_.record_latch = v == "true";
    if(json_find_value(s, "Midi In Channel", &v))
        opts_.midi_ch_in = std::max(0, std::min(15, atoi(v.c_str()) - 1));
    if(json_find_value(s, "Midi Out Channel", &v))
        opts_.midi_ch_out = std::max(0, std::min(15, atoi(v.c_str()) - 1));
    if(json_find_value(s, "Tape Slew On", &v))
        opts_.tape_slew = v == "true";
    if(json_find_value(s, "Monitor Position", &v))
        opts_.monitor_pos = std::max(0, std::min(2, atoi(v.c_str())));
    if(json_find_value(s, "Pitch Quantize In Shift Menu", &v))
        opts_.ps_quant = v == "true";
    if(json_find_value(s, "Split Delay", &v))
        opts_.split_delay = v == "true";
    if(json_find_value(s, "Pad Velocity", &v))
        key_velocity = v == "true" ? -1.f : 127.f;
}

void MunchiEngine::SaveOptions()
{
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\n\t\"chompi\": [\n"
             "\t\t{\n\t\t\t\"name\": \"Record Latch\",\n\t\t\t\"value\": %s\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Midi In Channel\",\n\t\t\t\"value\": %d\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Midi Out Channel\",\n\t\t\t\"value\": %d\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Tape Slew On\",\n\t\t\t\"value\": %s\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Monitor Position\",\n\t\t\t\"value\": %d\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Pitch Quantize In Shift Menu\",\n\t\t\t\"value\": %s\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Split Delay\",\n\t\t\t\"value\": %s\n\t\t},\n"
             "\t\t{\n\t\t\t\"name\": \"Pad Velocity\",\n\t\t\t\"value\": %s\n\t\t}\n"
             "\t]\n}",
             opts_.record_latch ? "true" : "false", opts_.midi_ch_in + 1,
             opts_.midi_ch_out + 1, opts_.tape_slew ? "true" : "false",
             opts_.monitor_pos, opts_.ps_quant ? "true" : "false",
             opts_.split_delay ? "true" : "false",
             key_velocity < 0.f ? "true" : "false");
    if(options_ready_.load())
        return; // the worker has not taken the last one yet; next change will
    options_snapshot_ = buf;
    options_ready_.store(true, std::memory_order_release);
}

void MunchiEngine::WorkerOptionsWrite()
{
    std::string p   = card_dir_ + "/options.json";
    std::string tmp = p + ".tmp";
    if(FILE *f = fopen(tmp.c_str(), "wb"))
    {
        fwrite(options_snapshot_.data(), 1, options_snapshot_.size(), f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        rename(tmp.c_str(), p.c_str());
    }
    options_ready_.store(false, std::memory_order_release);
}

/* ---- MenuPage ---------------------------------------------------------- */

void MunchiEngine::MenuFocusGained()
{
    menu_chompi_pressed_ = true;
    if(opts_.ps_quant)
    {
        engine_.ResetGlobalPitchQuant();
        engine_.ResetLooperPitchQuant();
    }
    ss_bank_       = engine_.GetBank();
    copy_src_      = kSlotNone;
    selected_slot_ = engine_.GetVoiceSlot();
    preset_mode_   = PM_NONE;
}

void MunchiEngine::MenuSetVoiceSlot(size_t slot)
{
    size_t mode = static_cast<size_t>(engine_.GetVoiceMode());
    size_t bank = engine_.GetBank();

    if(!presets_.IsValid(mode, bank, slot))
    {
        enc_values_[0][0] = enc_defaults[0][0];
        enc_values_[0][1] = enc_defaults[0][1];
        enc_values_[0][2] = enc_defaults[0][2];
        enc_values_[1][0] = enc_defaults[1][0];
        enc_values_[1][1] = enc_defaults[1][1];
        enc_values_[1][2] = enc_defaults[1][2];
        engine_.SetAutoLoop(true);
        engine_.SetSustainActive(true);
        engine_.SetPan(.5f);
    }
    else
    {
        enc_values_[0][0] = presets_.GetValue(mode, bank, slot, 0);
        enc_values_[0][1] = presets_.GetValue(mode, bank, slot, 1);
        enc_values_[0][2] = presets_.GetValue(mode, bank, slot, 2);
        enc_values_[1][1] = presets_.GetValue(mode, bank, slot, 3);
        enc_values_[1][2] = presets_.GetValue(mode, bank, slot, 4);
        engine_.SetAutoLoop(presets_.GetValue(mode, bank, slot, 5));
        engine_.SetSustainActive(presets_.GetValue(mode, bank, slot, 6));
        enc_values_[1][0] = presets_.GetValue(mode, bank, slot, 7);
        engine_.SetPan(presets_.GetValue(mode, bank, slot, 8));
    }

    float pitch = PitchFromEnc(enc_values_[0][0]);
    engine_.SetGlobalPitch(fabsf(pitch));
    engine_.SetReverse(pitch < 0.f);

    engine_.SetStartPointForce(enc_values_[0][1]);
    engine_.SetEndPointForce(enc_values_[0][2]);

    engine_.SetGain(enc_values_[1][0]);
    engine_.SetAttack(enc_values_[1][1]);
    engine_.SetDecay(enc_values_[1][2]);

    engine_.SetVoiceSlot(slot, true);
}

bool MunchiEngine::MenuButton(int buttonID, bool rising)
{
    switch(buttonID)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5: break;

        case ENC_4_SW: // pitch knob
        {
            const int page = knob_page_[0];
            if(rising)
            {
                if(page == 0)
                {
                    enc_values_[0][0] = enc_defaults[0][0];
                    engine_.SetGlobalPitch(1.f);
                    engine_.SetReverse(false);
                    engine_.ResetGlobalPitchQuant();
                }
                else if(page == 1)
                {
                    enc_values_[1][0] = enc_defaults[1][0];
                    engine_.SetPan(.5f);
                    engine_.SetGain(.704f);
                }
                DumpValuePresets();
            }
            break;
        }

        case ENC_5_SW: return false; // looper pitch reset falls through

        case ENC_1_SW: // attack knob
            if(rising)
            {
                engine_.ToggleAutoLoop();
                DumpValuePresets();
            }
            break;

        case ENC_2_SW: // decay knob
            if(rising)
            {
                engine_.ToggleSustainActive();
                DumpValuePresets();
            }
            break;

        case ENC_6_SW: // volume
            if(rising)
                engine_.IncrementMonitorMode();
            break;

        case ENC_3_SW: // magic wand
            if(rising)
            {
                enc_values_[0][3] = enc_defaults[0][3];
                enc_values_[1][3] = enc_defaults[1][3];
                enc_values_[2][3] = enc_defaults[2][3];

                engine_.SetReverb(enc_values_[0][3]);
                engine_.SetDelayFeedback(enc_values_[0][3]);
                engine_.SetSaturate(enc_values_[1][3]);
                engine_.SetFilter(enc_values_[2][3]);

                delay_time_ = .5f;
                resonance_  = 0.f;
                warble_     = 0.f;
                engine_.SetFilterResonance(resonance_);
                engine_.SetDelayTime(delay_time_);
                engine_.SetWarble(warble_);

                if(opts_.split_delay)
                    enc_values_[0][3] = .5f;
            }
            break;

        case SW_TOG: break;

        case KEY_26: // chompi
        {
            menu_chompi_pressed_ = rising;
            if(rising && selected_slot_ != kSlotNone)
            {
                if(preset_mode_ == PM_SAVE_SEL)
                {
                    engine_.StopAllVoices();
                    CopyRequest req;
                    req.src       = 15;
                    req.src_bank  = cs_bank_;
                    req.src_mode  = cs_mode_;
                    req.dest      = selected_slot_;
                    req.dest_bank = ss_bank_;
                    req.dest_mode = ss_mode_;
                    req.set       = selected_slot_ != 16;
                    req.is_chompi = CopyRequest::FROM;
                    req.is_looper = selected_slot_ == 16 ? CopyRequest::TO
                                                         : CopyRequest::NONE;
                    QueueCopy(req);
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_SAVING;
                    presets_.Save(ss_mode_, ss_bank_, selected_slot_);
                }
                else if(preset_mode_ == PM_COPY_DEST)
                {
                    engine_.StopAllVoices();
                    CopyRequest req;
                    req.src       = copy_src_;
                    req.src_bank  = cs_bank_;
                    req.src_mode  = cs_mode_;
                    req.dest      = selected_slot_;
                    req.dest_bank = ss_bank_;
                    req.dest_mode = ss_mode_;
                    if(copy_src_ == 15)
                        req.is_chompi = CopyRequest::FROM;
                    else if(copy_src_ == 16)
                        req.is_looper = CopyRequest::FROM;
                    if(selected_slot_ == 15)
                        req.is_chompi = CopyRequest::TO;
                    else if(selected_slot_ == 16)
                        req.is_looper = CopyRequest::TO;
                    req.set = !(selected_slot_ == 16)
                              && !(selected_slot_ == 15
                                   && ss_mode_ == int(VoiceMode::CUBBI));
                    QueueCopy(req);
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_COPYING;
                    presets_.Copy(cs_mode_, cs_bank_, copy_src_, ss_mode_,
                                  ss_bank_, selected_slot_);
                }
                else if(preset_mode_ == PM_ERASE_SEL)
                {
                    engine_.StopAllVoices();
                    engine_.EraseStart(selected_slot_, ss_bank_, VoiceMode(ss_mode_));
                    preset_mode_  = PM_ERASING;
                    blink_startt_ = daisy::System::now_ms;
                    presets_.Invalidate(ss_mode_, ss_bank_, selected_slot_);
                }
            }
            return false;
        }

        case KEY_16: // Jammi mode bank
        case KEY_17: // Cubbi mode bank
            if(rising)
            {
                VoiceMode mode = buttonID == KEY_16 ? VoiceMode::JAMMI
                                                    : VoiceMode::CUBBI;
                if(engine_.GetVoiceMode() == mode)
                {
                    engine_.IncrementBank();
                    if(mode == VoiceMode::CUBBI)
                        engine_.StopAllVoices();
                }
                else
                {
                    engine_.SetVoiceMode(mode);
                    if(mode == VoiceMode::JAMMI)
                    {
                        engine_.SetBank(engine_.GetVoiceBank());
                        MenuSetVoiceSlot(engine_.GetVoiceSlot());
                    }
                    engine_.StopAllVoices();
                }
            }
            else
                return false;
            break;

        case KEY_18: // mic in
        case KEY_19: // aux in
        case KEY_20: // resample
            if(rising)
            {
                InputSource source = buttonID == KEY_18   ? InputSource::MIC
                                     : buttonID == KEY_19 ? InputSource::LINE_IN
                                                          : InputSource::RESAMPLE;
                engine_.SetInputSource(source);
            }
            else
                return false;
            break;

        case KEY_21: // fx pre looper
        case KEY_22: // fx post looper
            if(rising)
                engine_.SetFxPreLooper(buttonID == KEY_21);
            else
                return false;
            break;

        case KEY_23: // erase
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_ERASE_SEL;
                else if(preset_mode_ == PM_ERASE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                selected_slot_ = kSlotNone;
                ss_bank_       = kSlotNone;
                ss_mode_       = int(VoiceMode::LAST);
            }
            else
                return false;
            break;

        case KEY_24: // copy
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_COPY_SRC;
                else if(preset_mode_ == PM_COPY_SRC || preset_mode_ == PM_COPY_DEST)
                    preset_mode_ = PM_NONE;
                else
                    break;
                copy_src_      = kSlotNone;
                selected_slot_ = kSlotNone;
                ss_bank_       = kSlotNone;
                cs_bank_       = kSlotNone;
                ss_mode_       = int(VoiceMode::LAST);
                cs_mode_       = int(VoiceMode::LAST);
            }
            else
                return false;
            break;

        case KEY_25: // save
            if(rising && engine_.GetFileExists(14))
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_SAVE_SEL;
                else if(preset_mode_ == PM_SAVE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                copy_src_      = kSlotNone;
                selected_slot_ = kSlotNone;
                ss_bank_       = kSlotNone;
                cs_bank_       = kSlotNone;
                ss_mode_       = int(VoiceMode::LAST);
                cs_mode_       = int(VoiceMode::LAST);
            }
            else if(!rising)
                return false;
            break;

        // white keys and play/pause
        default:
            if((buttonID == KEY_27 || buttonID == KEY_28) && preset_mode_ == PM_NONE)
            {
                const float gain = buttonID == KEY_27 ? -.1f : .1f;
                engine_.IncrementLooperDubGain(gain);
            }

            if(rising)
            {
                size_t slot_req = KeyToSlot(buttonID);
                if(buttonID == KEY_27 || buttonID == KEY_28)
                    slot_req = 16;

                if(slot_req == kSlotNone)
                {
                }
                else if(slot_req == 16)
                {
                    if(preset_mode_ == PM_COPY_DEST && copy_src_ != 16)
                    {
                        selected_slot_ = slot_req;
                        ss_bank_       = engine_.GetBank();
                        ss_mode_       = int(engine_.GetVoiceMode());
                    }
                    else if(preset_mode_ == PM_COPY_SRC && !engine_.GetLooperIsEmpty())
                    {
                        copy_src_    = slot_req;
                        cs_bank_     = engine_.GetBank();
                        cs_mode_     = int(engine_.GetVoiceMode());
                        preset_mode_ = PM_COPY_DEST;
                    }
                }
                else if(preset_mode_ == PM_COPY_SRC
                        && engine_.GetFileExists(slot_req - 1))
                {
                    copy_src_    = slot_req;
                    cs_bank_     = engine_.GetBank();
                    cs_mode_     = int(engine_.GetVoiceMode());
                    preset_mode_ = PM_COPY_DEST;
                }
                else if(preset_mode_ == PM_COPY_DEST
                        && (copy_src_ != slot_req || engine_.GetBank() != cs_bank_
                            || int(engine_.GetVoiceMode()) != cs_mode_))
                {
                    ss_bank_       = engine_.GetBank();
                    ss_mode_       = int(engine_.GetVoiceMode());
                    selected_slot_ = slot_req;
                }
                else if(preset_mode_ == PM_NONE && engine_.GetFileExists(slot_req - 1)
                        && engine_.GetVoiceMode() == VoiceMode::JAMMI)
                {
                    ss_bank_       = engine_.GetBank();
                    ss_mode_       = int(engine_.GetVoiceMode());
                    selected_slot_ = slot_req;
                    MenuSetVoiceSlot(selected_slot_);
                }
                else if(preset_mode_ == PM_ERASE_SEL && slot_req != 15
                        && engine_.GetFileExists(slot_req - 1))
                {
                    ss_bank_       = engine_.GetBank();
                    ss_mode_       = int(engine_.GetVoiceMode());
                    selected_slot_ = slot_req;
                }
                else if(preset_mode_ == PM_SAVE_SEL && slot_req != 15)
                {
                    ss_bank_       = engine_.GetBank();
                    ss_mode_       = int(engine_.GetVoiceMode());
                    selected_slot_ = slot_req;
                }
            }
            else if(buttonID < 29)
            {
                return false; // allow releasing notes in shift menu
            }
            break;
    }
    return true;
}

void MunchiEngine::MenuEncoder(int encoderID, int turns)
{
    int   page = knob_page_[encoderID];
    float inc  = turns * kEncoderCoarseStep;

    if(preset_mode_ != PM_NONE)
        return;

    if(encoderID == 0)
    {
        if(page == 0) // stepped pitch
        {
            if(opts_.ps_quant)
                enc_values_[0][0] = engine_.SetGlobalPitchQuantized(turns, enc_values_[0][0]);
            else
            {
                enc_values_[0][0] += turns * kEncoderFineStep;
                enc_values_[0][0] = daisysp::fclamp(enc_values_[0][0], 0.f, 1.f);
                engine_.SetGlobalPitchFree(enc_values_[0][0]);
            }
        }
        else if(page == 1) // pan
        {
            engine_.SetPan(engine_.GetPan() + inc);
        }
        DumpValuePresets();
    }
    // move sample window (both start and end pos)
    else if((encoderID == 1 || encoderID == 2) && page == 0)
    {
        if(!(turns > 0 && enc_values_[0][2] + inc > 1.f)
           && !(turns < 0 && enc_values_[0][1] + inc < 0.f))
        {
            enc_values_[0][1] += inc;
            enc_values_[0][2] += inc;
            engine_.SetStartPointForce(enc_values_[0][1]);
            engine_.SetEndPointForce(enc_values_[0][2]);
            DumpValuePresets();
        }
        knob_page_[1] = knob_page_[2] = 0;
    }
    // set both attack and decay at once
    else if((encoderID == 1 || encoderID == 2) && page == 1)
    {
        float val = encoderID == 1 ? enc_values_[1][1] : enc_values_[1][2];
        val       = daisysp::fclamp(val + inc, 0.f, 1.f);
        engine_.SetAttack(val);
        engine_.SetDecay(val);
        enc_values_[1][1] = val;
        enc_values_[1][2] = val;
        DumpValuePresets();
        knob_page_[1] = knob_page_[2] = 1;
    }
    else if(encoderID == 3)
    {
        if(page == 0)
        {
            delay_time_ = daisysp::fclamp(delay_time_ + inc, 0.f, 1.f);
            engine_.SetDelayTime(delay_time_);
        }
        else if(page == 1)
        {
            warble_ = daisysp::fclamp(warble_ + inc, 0.f, 1.f);
            engine_.SetWarble(warble_);
        }
        else if(page == 2)
        {
            resonance_ = daisysp::fclamp(resonance_ + inc, 0.f, 1.f);
            engine_.SetFilterResonance(resonance_);
        }
    }
    else if(encoderID == 4)
    {
        if(opts_.ps_quant)
            enc_values_[0][4] = engine_.SetLooperPitchQuantized(turns, enc_values_[0][4]);
        else
        {
            enc_values_[0][4] += turns * kEncoderFineStep;
            enc_values_[0][4] = daisysp::fclamp(enc_values_[0][4], 0.f, 1.f);
            engine_.SetLooperPitchFree(enc_values_[0][4]);
        }
    }
    else if(encoderID == 5)
    {
        final_comp_ = daisysp::fclamp(final_comp_ + inc, 0.f, 1.f);
        engine_.SetFinalComp(final_comp_);
    }
}

/* IsClosable(). The firmware also closed an idle menu when the mode switch
 * went to record; on Move the menu is held open by Shift rather than by the
 * CHOMPI key, so that clause has nothing to watch and is dropped. */
bool MunchiEngine::MenuIsClosable()
{
    const uint32_t now = daisy::System::now_ms;
    if(now - blink_startt_ > 1000
       && ((preset_mode_ == PM_SAVING && !Copying())
           || (preset_mode_ == PM_COPYING && !Copying())
           || (preset_mode_ == PM_ERASING && !engine_.IsErasing())
           || (preset_mode_ == PM_NONE && !menu_chompi_pressed_)))
    {
        if((preset_mode_ == PM_SAVING || preset_mode_ == PM_COPYING)
           && selected_slot_ == 16)
        {
            engine_.LooperOpenFile();
            enc_values_[0][4] = enc_defaults[0][4];
        }
        else if((preset_mode_ == PM_COPYING || preset_mode_ == PM_SAVING)
                && ss_mode_ != int(VoiceMode::CUBBI))
        {
            MenuSetVoiceSlot(selected_slot_);
        }
        else if(preset_mode_ == PM_ERASING
                && engine_.GetVoiceMode() == VoiceMode::JAMMI
                && engine_.GetVoiceSlot() == selected_slot_)
        {
            MenuSetVoiceSlot(15);
        }

        if(!opts_.ps_quant)
        {
            engine_.ResetGlobalPitchQuant();
            engine_.ResetLooperPitchQuant();
        }
        preset_mode_ = PM_NONE;
        return true;
    }
    return false;
}

/* ---- copier ------------------------------------------------------------- */

void MunchiEngine::QueueCopy(const CopyRequest &req)
{
    if(Copying())
        return;
    // FileCopier::CopyStart, engine side
    if(req.dest < 16)
        engine_.SetFileExists(req.dest - 1, req.dest_bank, VoiceMode(req.dest_mode), true);
    pending_copy_ = req;
    copying_.store(true, std::memory_order_release);
    copy_posted_.store(true, std::memory_order_release);
}

void MunchiEngine::CopyFinished()
{
    const CopyRequest &req = pending_copy_;
    if(req.set)
    {
        engine_.SetBank(req.dest_bank);
        engine_.SetVoiceMode(VoiceMode(req.dest_mode));
        engine_.SetVoiceSlot(req.dest, false);
    }
    engine_.SetAllCopyOccurred();
    copying_.store(false, std::memory_order_release);
}

/* ---- MIDI --------------------------------------------------------------- */

void MunchiEngine::SendNoteOn(int note, int vel)
{
    int w = (midi_out_w_ + 1) & 63;
    if(w == midi_out_r_)
        return;
    midi_out_[midi_out_w_][0] = 0x90 | ((opts_.midi_ch_out + midi_channel_) & 15);
    midi_out_[midi_out_w_][1] = note & 127;
    midi_out_[midi_out_w_][2] = vel & 127;
    midi_out_w_               = w;
}

void MunchiEngine::SendNoteOff(int note)
{
    int w = (midi_out_w_ + 1) & 63;
    if(w == midi_out_r_)
        return;
    midi_out_[midi_out_w_][0] = 0x80 | ((opts_.midi_ch_out + midi_channel_) & 15);
    midi_out_[midi_out_w_][1] = note & 127;
    midi_out_[midi_out_w_][2] = 127;
    midi_out_w_               = w;
}

void MunchiEngine::SendCC(int cc, int val)
{
    int w = (midi_out_w_ + 1) & 63;
    if(w == midi_out_r_)
        return;
    midi_out_[midi_out_w_][0] = 0xB0 | ((opts_.midi_ch_out + midi_channel_) & 15);
    midi_out_[midi_out_w_][1] = cc & 127;
    midi_out_[midi_out_w_][2] = val < 0 ? 0 : (val > 127 ? 127 : val);
    midi_out_w_               = w;
}

bool MunchiEngine::PopMidiOut(uint8_t out[3])
{
    if(midi_out_r_ == midi_out_w_)
        return false;
    memcpy(out, midi_out_[midi_out_r_], 3);
    midi_out_r_ = (midi_out_r_ + 1) & 63;
    return true;
}

/* ui.h ProcessMidi */
void MunchiEngine::Midi(const uint8_t *msg, int len)
{
    if(len < 3)
        return;
    uint8_t type = msg[0] & 0xF0;
    uint8_t ch   = msg[0] & 0x0F;
    if(ch != opts_.midi_ch_in)
        return;

    if(type == 0x90 && msg[2] > 0)
    {
        int key = msg[1] - 24;
        if(key > 48 || key < 0)
            return;
        if(engine_.GetVoiceMode() == VoiceMode::CUBBI)
        {
            size_t slot = KeyToSlot(midi2key[key]);
            if(slot == kSlotNone)
                return;
            OpenCubbiSlot(slot);
        }
        engine_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, key - 36,
                                                 midi2key[key], msg[2] + 1));
        if(engine_.GetLooperRecordArm())
            engine_.ToggleLooperRecord();
    }
    else if(type == 0x80 || (type == 0x90 && msg[2] == 0))
    {
        int key = msg[1] - 24;
        if(key > 48 || key < 0)
            return;
        engine_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, key - 36,
                                                 midi2key[key], msg[2] + 1));
    }
    else if(type == 0xB0)
    {
        if(menu_active_)
            return;
        uint8_t cc  = msg[1];
        uint8_t val = msg[2];
        if(cc >= 20 && cc < 26)
        {
            uint8_t knob = cc - 20;
            if(knob == 4 && !engine_.IsLooperPlaying())
                return;
            NormalEncoder(knob, val, 1);
        }
        else if(cc == 26 || cc == 27)
        {
            const uint8_t idx  = cc - 26;
            const bool    last = key_cc_[idx];
            if(val > 84)
                key_cc_[idx] = true;
            else if(val < 42)
                key_cc_[idx] = false;
            if(!last && key_cc_[idx])
                Button(cc + 7, true);
            else if(last && !key_cc_[idx])
                Button(cc + 7, false);
        }
    }
}

/* ---- card worker -------------------------------------------------------- */

std::string MunchiEngine::SlotPath(int mode, int bank, int slot) const
{
    char name[64];
    snprintf(name, sizeof(name), "%s_%c%d.wav", mode == 0 ? "jammi" : "cubbi",
             'a' + bank, slot);
    return card_dir_ + "/" + name;
}

static bool file_exists(const std::string &p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static int copy_file(const std::string &from, const std::string &to)
{
    FILE *in = fopen(from.c_str(), "rb");
    if(!in)
        return -1;
    std::string tmp = to + ".tmp";
    FILE       *out = fopen(tmp.c_str(), "wb");
    if(!out)
    {
        fclose(in);
        return -1;
    }
    char   buf[65536];
    size_t n;
    int    ok = 1;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if(fwrite(buf, 1, n, out) != n)
        {
            ok = 0;
            break;
        }
    fclose(in);
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    if(!ok)
    {
        unlink(tmp.c_str());
        return -1;
    }
    return rename(tmp.c_str(), to.c_str());
}

static void mkdirs(const std::string &path)
{
    std::string p;
    for(size_t i = 0; i < path.size(); i++)
    {
        p += path[i];
        if(path[i] == '/' && p.size() > 1)
            mkdir(p.c_str(), 0775);
    }
    mkdir(path.c_str(), 0775);
}

void MunchiEngine::StartCardWorker(const std::string &card_dir,
                                   const std::string &factory_dir)
{
    card_dir_    = card_dir;
    factory_dir_ = factory_dir;

    // Everything that touches the card happens on the worker -- including
    // the first-launch copy of the factory card, which is ~100 MB and would
    // otherwise hold the device dark before the SPI loop starts.
    worker_run_.store(true);
    worker_ = std::thread([this] { WorkerMain(); });
}

void MunchiEngine::StopCardWorker()
{
    if(!worker_run_.load())
        return;
    // flush unsaved presets before the worker goes
    if(presets_.updated && presets_known_.load() && !loaded_presets_.load())
    {
        presets_snapshot_ = presets_.Serialize();
        presets_.updated  = false;
        presets_ready_.store(true);
    }
    worker_run_.store(false);
    if(worker_.joinable())
        worker_.join();
    if(presets_ready_.load())
        WorkerPresetsWrite();
    if(options_ready_.load())
        WorkerOptionsWrite();
}

int MunchiEngine::LoadCardBlocking(const std::string &card_dir)
{
    card_dir_ = card_dir;
    WorkerScan();
    int n = 0;
    while(WorkerLoadOne())
        n++;
    card_ready_.store(true);
    return n;
}

void MunchiEngine::WorkerSeed()
{
    const std::string marker = card_dir_ + "/.munchi-card";
    if(file_exists(marker) || factory_dir_.empty())
        return;
    // First run: the factory card, file by file, never over something there.
    for(int m = 0; m < kModes; m++)
        for(int b = 0; b < kBanks; b++)
            for(int s = 1; s <= kSlots; s++)
            {
                char name[64];
                snprintf(name, sizeof(name), "%s_%c%d.wav",
                         m == 0 ? "jammi" : "cubbi", 'a' + b, s);
                std::string from = factory_dir_ + "/" + name;
                std::string to   = card_dir_ + "/" + name;
                if(file_exists(from) && !file_exists(to))
                    copy_file(from, to);
            }
    for(const char *f : {"presets.json", "options.json"})
    {
        std::string from = factory_dir_ + "/" + f;
        std::string to   = card_dir_ + "/" + f;
        if(file_exists(from) && !file_exists(to))
            copy_file(from, to);
    }
    if(FILE *f = fopen(marker.c_str(), "wb"))
    {
        fputs("Munchi card. Delete this file to re-copy missing factory samples.\n", f);
        fclose(f);
    }
}

void MunchiEngine::WorkerScan()
{
    int total = 0;
    for(int m = 0; m < kModes; m++)
        for(int b = 0; b < kBanks; b++)
            for(int s = 1; s <= kSlots; s++)
            {
                bool e = file_exists(SlotPath(m, b, s));
                store_.SetExists(m, b, s, e);
                total += e;
            }
    total_count_.store(total);
}

/* Loads the most wanted sample that is on the card but not in RAM. Order:
 * the playing mode's current bank, the other mode's same bank, then the rest
 * -- so the first notes after launch are the ones you are about to play. */
bool MunchiEngine::WorkerLoadOne()
{
    const int wm = want_mode_.load(), wb = want_bank_.load();
    int       order_m[kModes * kBanks], order_b[kModes * kBanks], n = 0;
    order_m[n] = wm;
    order_b[n++] = wb;
    order_m[n] = 1 - wm;
    order_b[n++] = wb;
    for(int b = 0; b < kBanks; b++)
        for(int m = 0; m < kModes; m++)
            if(b != wb)
            {
                order_m[n]   = m;
                order_b[n++] = b;
            }

    for(int i = 0; i < n; i++)
        for(int s = 1; s <= kSlots; s++)
        {
            int m = order_m[i], b = order_b[i];
            if(!store_.Exists(m, b, s) || store_.Loaded(m, b, s))
                continue;
            int16_t *pcm = nullptr;
            size_t   frames = 0;
            char     err[64];
            if(wav_load_48k_stereo(SlotPath(m, b, s).c_str(), &pcm, &frames,
                                   err, sizeof(err))
               != 0)
            {
                // unreadable: the firmware would have played silence; say so
                last_error_ = SlotPath(m, b, s) + ": " + err;
                store_.SetExists(m, b, s, false);
                return true;
            }
            Sample *smp        = new Sample();
            smp->mem.mem      = pcm;
            smp->mem.length   = frames * 2;
            smp->mem.capacity = frames * 2;
            WorkerRetire(store_.Publish(m, b, s, smp));
            loaded_count_.fetch_add(1);
            return true;
        }
    return false;
}

void MunchiEngine::WorkerRetire(Sample *s)
{
    if(!s)
        return;
    s->retired_at_block = store_.engine_blocks.load();
    retired_.push_back(s);
}

void MunchiEngine::WorkerFreeRetired(bool force)
{
    const uint64_t now = store_.engine_blocks.load();
    for(size_t i = 0; i < retired_.size();)
    {
        Sample *s = retired_[i];
        if(force || (s->refs.load() == 0 && now > s->retired_at_block + 2))
        {
            free(s->mem.mem);
            delete s;
            retired_[i] = retired_.back();
            retired_.pop_back();
        }
        else
            i++;
    }
}

static void write_double_file(const std::string &path, const int16_t *pcm,
                              size_t frames)
{
    // TAPE's "_double" companion: every other frame, so a CHOMPI reading
    // this card does not regenerate it.
    size_t   n = frames / 2;
    int16_t *d = (int16_t *)malloc((n ? n : 1) * 2 * sizeof(int16_t));
    if(!d)
        return;
    for(size_t i = 0; i < n; i++)
    {
        d[i * 2]     = pcm[i * 4];
        d[i * 2 + 1] = pcm[i * 4 + 1];
    }
    std::string dp = path.substr(0, path.size() - 4) + "_double.wav";
    wav_write_48k_stereo(dp.c_str(), d, n);
    free(d);
}

void MunchiEngine::WorkerCopy(const CopyRequest &r)
{
    // source
    const int16_t *src    = nullptr;
    size_t         frames = 0;
    int16_t       *owned  = nullptr;
    Sample        *held   = nullptr;
    if(r.is_chompi == CopyRequest::FROM)
    {
        src    = chompi_buff_.mem;
        frames = chompi_buff_.length / 2;
    }
    else if(r.is_looper == CopyRequest::FROM)
    {
        src    = loop_buff_.mem;
        frames = loop_buff_.length / 2;
    }
    else
    {
        held = store_.Acquire(r.src_mode, r.src_bank, r.src);
        if(held)
        {
            src    = held->mem.mem;
            frames = held->mem.length / 2;
        }
        else
        {
            char err[64];
            if(wav_load_48k_stereo(SlotPath(r.src_mode, r.src_bank, r.src).c_str(),
                                   &owned, &frames, err, sizeof(err))
               == 0)
                src = owned;
        }
    }

    if(src && frames)
    {
        if(r.is_chompi == CopyRequest::TO || r.is_looper == CopyRequest::TO)
        {
            RamBufferMemory &dst = r.is_chompi == CopyRequest::TO ? chompi_buff_
                                                                  : loop_buff_;
            size_t n = frames * 2 < dst.capacity ? frames * 2 : dst.capacity;
            memcpy(dst.mem, src, n * sizeof(int16_t));
            dst.length = n - (n % 2);
        }
        else
        {
            std::string path = SlotPath(r.dest_mode, r.dest_bank, r.dest);
            wav_write_48k_stereo(path.c_str(), src, frames);
            write_double_file(path, src, frames);
            int16_t *copy = (int16_t *)malloc(frames * 2 * sizeof(int16_t));
            if(copy)
            {
                memcpy(copy, src, frames * 2 * sizeof(int16_t));
                Sample *smp        = new Sample();
                smp->mem.mem      = copy;
                smp->mem.length   = frames * 2;
                smp->mem.capacity = frames * 2;
                WorkerRetire(store_.Publish(r.dest_mode, r.dest_bank, r.dest, smp));
            }
            store_.SetExists(r.dest_mode, r.dest_bank, r.dest, true);
        }
    }
    SampleStore::Release(held);
    free(owned);
    copy_done_.store(true, std::memory_order_release);
}

void MunchiEngine::WorkerErase(int mode, int bank, int slot)
{
    std::string p = SlotPath(mode, bank, slot);
    unlink(p.c_str());
    std::string dp = p.substr(0, p.size() - 4) + "_double.wav";
    unlink(dp.c_str());
    WorkerRetire(store_.Publish(mode, bank, slot, nullptr));
    store_.SetExists(mode, bank, slot, false);
    engine_.EraseFinished();
}

void MunchiEngine::WorkerPresetsWrite()
{
    std::string p   = card_dir_ + "/presets.json";
    std::string tmp = p + ".tmp";
    if(FILE *f = fopen(tmp.c_str(), "wb"))
    {
        fwrite(presets_snapshot_.data(), 1, presets_snapshot_.size(), f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        rename(tmp.c_str(), p.c_str());
    }
    presets_ready_.store(false, std::memory_order_release);
}

void MunchiEngine::WorkerMain()
{
    // A plain worker: ordinary priority, off the SPI core.
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif

    mkdirs(card_dir_);
    seeding_.store(true);
    WorkerSeed();
    seeding_.store(false);

    // presets.json, handed to the audio thread whole (it owns presets_)
    std::string pj = card_dir_ + "/presets.json";
    if(FILE *f = fopen(pj.c_str(), "rb"))
    {
        std::string s;
        char        buf[4096];
        size_t      n;
        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
        Presets *p = new Presets();
        p->Init(preset_defaults);
        if(p->Parse(s.c_str()))
        {
            p->updated = false;
            loaded_presets_.store(p, std::memory_order_release);
        }
        else
            delete p;
    }
    presets_known_.store(true, std::memory_order_release);

    WorkerScan();

    auto     last_scan = std::chrono::steady_clock::now();
    while(worker_run_.load())
    {
        bool busy = false;

        if(copy_posted_.load(std::memory_order_acquire))
        {
            copy_posted_.store(false);
            WorkerCopy(pending_copy_);
            busy = true;
        }

        while(!engine_.card_fifo.IsEmpty())
        {
            daisy::CardRequest req = engine_.card_fifo.PopFront();
            if(req.type == daisy::CardRequest::Type::ERASE)
                WorkerErase(req.mode, req.bank, req.slot);
            busy = true;
        }

        if(presets_ready_.load(std::memory_order_acquire))
        {
            WorkerPresetsWrite();
            busy = true;
        }

        if(options_ready_.load(std::memory_order_acquire))
        {
            WorkerOptionsWrite();
            busy = true;
        }

        if(!busy && WorkerLoadOne())
            busy = true;
        else if(!busy)
            card_ready_.store(true);

        WorkerFreeRetired(false);
        if(Presets *p = adopted_presets_.exchange(nullptr))
            delete p;

        // Pick up files dropped on the card from the web manager.
        auto now = std::chrono::steady_clock::now();
        if(now - last_scan > std::chrono::seconds(5) && !Copying())
        {
            last_scan = now;
            for(int m = 0; m < kModes; m++)
                for(int b = 0; b < kBanks; b++)
                    for(int s = 1; s <= kSlots; s++)
                    {
                        bool on_card = file_exists(SlotPath(m, b, s));
                        if(on_card && !store_.Exists(m, b, s))
                        {
                            store_.SetExists(m, b, s, true);
                            total_count_.fetch_add(1);
                        }
                        else if(!on_card && store_.Exists(m, b, s)
                                && store_.Loaded(m, b, s))
                        {
                            WorkerRetire(store_.Publish(m, b, s, nullptr));
                            store_.SetExists(m, b, s, false);
                        }
                    }
        }

        if(!busy)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

/* ---- harness ------------------------------------------------------------ */

void MunchiEngine::ScriptCommand(const char *op, const char *a, float b)
{
    if(!strcmp(op, "mode"))
    {
        OpenMenu();
        Button(!strcmp(a, "kit") ? KEY_17 : KEY_16, true);
        Button(!strcmp(a, "kit") ? KEY_17 : KEY_16, false);
        menu_chompi_pressed_ = false;
    }
    else if(!strcmp(op, "slot"))
    {
        OpenMenu();
        int slot = atoi(a);
        // white keys by slot, the inverse of KeyToSlot
        static const int slot_key[16] = {0,  15, 8,  9,  10, 11, 16, 17,
                                         18, 19, 20, 24, 25, 26, 27, 28};
        if(slot >= 1 && slot <= 15)
        {
            Button(slot_key[slot], true);
            Button(slot_key[slot], false);
        }
        menu_chompi_pressed_ = false;
    }
    else if(!strcmp(op, "bank"))
    {
        int target = atoi(a);
        OpenMenu();
        int key = engine_.GetVoiceMode() == VoiceMode::CUBBI ? KEY_17 : KEY_16;
        while(engine_.GetBank() != target)
        {
            Button(key, true);
            Button(key, false);
        }
        menu_chompi_pressed_ = false;
    }
    else if(!strcmp(op, "on"))
    {
        init_ignore_ = false;
        Button(atoi(a), true);
    }
    else if(!strcmp(op, "off"))
        Button(atoi(a), false);
    else if(!strcmp(op, "note"))
    {
        uint8_t m[3] = {uint8_t(0x90 | opts_.midi_ch_in), (uint8_t)atoi(a),
                        (uint8_t)(b < 0 ? 100 : b)};
        Midi(m, 3);
    }
    else if(!strcmp(op, "noteoff"))
    {
        uint8_t m[3] = {uint8_t(0x80 | opts_.midi_ch_in), (uint8_t)atoi(a), 0};
        Midi(m, 3);
    }
    else if(!strcmp(op, "enc"))
    {
        // enc <knob>.<page> <turns>
        int knob = 0, page = 0;
        sscanf(a, "%d.%d", &knob, &page);
        init_ignore_ = false;
        Encoder(knob, page, (int)b, false);
    }
    else if(!strcmp(op, "menc"))
    {
        int knob = 0, page = 0;
        sscanf(a, "%d.%d", &knob, &page);
        Encoder(knob, page, (int)b, true);
    }
    else if(!strcmp(op, "switch"))
        SetSwitch(atoi(a) != 0);
    else if(!strcmp(op, "hp"))
        SetHeadphones(atoi(a) != 0);
    else if(!strcmp(op, "rec"))
        RecordKey(atoi(a) != 0);
    else if(!strcmp(op, "menukey"))
        MenuKey(atoi(a) != 0);
    else if(!strcmp(op, "src"))
    {
        engine_.SetInputSource(!strcmp(a, "line") ? InputSource::LINE_IN
                               : !strcmp(a, "mic") ? InputSource::MIC
                                                   : InputSource::RESAMPLE);
    }
}

} // namespace munchi
