/* munchi_engine.h -- CHOMPI TAPE 2.0, minus the hardware.
 *
 * Three things live here, each a port of a firmware part:
 *
 *   audio      TAPE's Engine (tape/DSPEngine.h) at 48 kHz, wrapped in
 *              resamplers to and from Move's 44.1 kHz.
 *   controls   NormalPage + MenuPage + ui.h: what every key, encoder and
 *              button does. Events arrive as CHOMPI key ids (Hardware::SwId)
 *              and encoder indices, so the firmware's switch statements port
 *              unchanged; surface.cpp decides which Move control is which key.
 *   card       FileCopier + PresetManager + the SD side of Engine: a worker
 *              thread that owns every file operation.
 *
 * Threading: everything but the card worker runs on ONE thread (the SPI loop,
 * which is also the audio thread) -- the same shape as the firmware, whose UI
 * and audio shared a core. The worker talks to it through atomics and FIFOs
 * and never takes a lock the audio thread waits on.
 */
#pragma once
#include "daisy_compat.h"
#include "tape/DSPEngine.h"
#include "sample_store.h"
#include "resampler.h"
#include <atomic>
#include <thread>
#include <vector>
#include <string>

namespace munchi
{

/* CHOMPI Hardware::SwId, kept verbatim so KeyToSlot/key_map/midi2key hold. */
enum SwId
{
    ENC_1_SW = 0,
    ENC_2_SW,
    ENC_3_SW,
    ENC_4_SW,
    NC_6,
    KEY_26, // the CHOMPI key
    SW_TOG,
    KEY_16,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_17,
    KEY_18,
    KEY_19,
    KEY_1,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_10,
    KEY_20,
    KEY_21,
    KEY_22,
    KEY_11,
    KEY_12,
    KEY_13,
    KEY_14,
    KEY_15,
    KEY_23,
    KEY_24,
    KEY_25,
    ENC_6_SW,
    KEY_27, // play
    KEY_28, // loop
    NC_1,
    NC_2,
    NC_3,
    NC_4,
    NC_5,
    SR_LAST,
};
static constexpr int ENC_5_SW = 4; // "this one weird switch isn't on the SR"

/* The 25 keyboard keys, as MIDI notes 48..72 (key_map in NormalPage.h). */
int  KeyIdToNote(int key_id);  // -1 if not a keyboard key
int  NoteToKeyId(int note);    // -1 outside 48..72

struct Options
{
    bool record_latch  = false;
    int  midi_ch_in    = 0; // 0-based
    int  midi_ch_out   = 0;
    bool tape_slew     = true;
    int  monitor_pos   = 1; // MonitorMode: 0 HP, 1 BOTH, 2 SEND_RET
    bool ps_quant      = true; // "Pitch Quantize In Shift Menu"
    bool split_delay   = false;
};

/** PresetManager: per-slot sample settings, TAPE's presets.json format. */
struct Presets
{
    static constexpr int kControls = 9; // pitch start end att dec loop sus gain pan
    float values[kModes][kBanks][kSlots][kControls];
    bool  valid[kModes][kBanks][kSlots];
    float chompi_value[kControls];
    bool  chompi_valid = false;
    bool  updated      = false;

    void  Init(const float *defaults);
    bool  IsValid(size_t mode, size_t bank, size_t slot);
    float GetValue(size_t mode, size_t bank, size_t slot, size_t control);
    void  SetValue(float value, size_t mode, size_t bank, size_t slot, size_t control);
    void  Invalidate(uint8_t mode, uint8_t bank, uint8_t slot);
    void  Save(uint8_t mode, uint8_t bank, uint8_t slot);
    void  Copy(uint8_t ms, uint8_t bs, uint8_t ss, uint8_t mt, uint8_t bt, uint8_t st);
    bool  Parse(const char *json);
    std::string Serialize() const;
};

/** FileCopier::CopyRequest. Slot 15 = CHOMPI buffer, 16 = looper. */
struct CopyRequest
{
    enum RamDir { NONE, FROM, TO };
    int     src = 0, src_bank = 0, src_mode = 0;
    int     dest = 0, dest_bank = 0, dest_mode = 0;
    bool    set = false;
    RamDir  is_chompi = NONE, is_looper = NONE;
};

class MunchiEngine
{
  public:
    MunchiEngine();
    ~MunchiEngine();

    /** Allocates the buffers (the firmware's SDRAM). Call once, before audio. */
    int Init();

    /* ---- card ---------------------------------------------------------- */
    /** Worker: seed `card_dir` from `factory_dir` on first run, then load. */
    void StartCardWorker(const std::string &card_dir, const std::string &factory_dir);
    void StopCardWorker();
    /** Harness: load a card synchronously (no worker). */
    int  LoadCardBlocking(const std::string &card_dir);
    int  CardLoaded() const { return loaded_count_.load(); }
    int  CardTotal() const { return total_count_.load(); }
    bool CardReady() const { return card_ready_.load(); }
    bool CardSeeding() const { return seeding_.load(); }

    /* ---- audio (SPI thread) --------------------------------------------- */
    /** One Move block: `in` and `out` are int16 stereo interleaved, 44.1 kHz. */
    void ProcessHostBlock(const int16_t *in, int16_t *out, int frames);

    /* ---- controls (SPI thread) ----------------------------------------- */
    void Button(int sw_id, bool rising);
    /** An encoder detent (turns = +-n). `page` selects which of the knob's
     *  pages it edits; menu=true routes it through MenuPage's encoder rules. */
    void Encoder(int knob, int page, int turns, bool menu);
    /** A knob page is also what an encoder CLICK cycles on the hardware. */
    void SetKnobPage(int knob, int page) { knob_page_[knob] = page; }
    int  GetKnobPage(int knob) const { return knob_page_[knob]; }
    /** The toggle switch: true = play (CHOMPI key opens the menu),
     *  false = record (CHOMPI key records, input monitored). */
    void SetSwitch(bool play_mode);
    bool GetSwitch() const { return switch_state_; }
    /** Menu page ("shift"): opened by the CHOMPI key in play mode. */
    void OpenMenu();
    bool MenuActive() const { return menu_active_; }
    /** The CHOMPI key in its two roles, which Move splits across two
     *  buttons: MenuKey is the key in play mode (Shift on Move: opens and
     *  confirms the menu), RecordKey is the key in record mode (Sample on
     *  Move: records into the buffer), whatever the switch says. */
    void MenuKey(bool rising);
    void RecordKey(bool rising);
    /** An encoder CLICK, with the menu page's meaning where it has one:
     *  ENC_4_SW reset pitch/gain, ENC_1_SW auto-loop, ENC_2_SW sustain,
     *  ENC_3_SW reset FX, ENC_6_SW monitor mode, ENC_5_SW looper pitch. */
    void Click(int sw);
    /** Keys mode: select a slot (the menu's white-key press). */
    void SelectSlot(int slot);
    /** Clear the looper outright (the firmware's hold-both-for-2s). */
    void ClearLooper();
    /** Settings: read at start, written by the worker when changed. */
    void LoadOptions(const std::string &path);
    void SaveOptions();
    /** Headphones in (Move CC 115): the only time the mic may be heard. */
    void SetHeadphones(bool hp) { engine_.SetHeadphones(hp); }
    /** Jack detect (Move CC 114): line in vs internal mic. */
    void SetLineIn(bool plugged);
    /** External MIDI (ui.h ProcessMidi). */
    void Midi(const uint8_t *msg, int len);
    /** MIDI the firmware would have sent out (NormalPage's hw_->Send*). */
    bool PopMidiOut(uint8_t out[3]);

    /* ---- state for LEDs / display -------------------------------------- */
    daisy::Engine &Eng() { return engine_; }
    float  EncValue(int page, int knob) const { return enc_values_[page][knob]; }
    float  DelayTime() const { return delay_time_; }
    float  Resonance() const { return resonance_; }
    float  Warble() const { return warble_; }
    float  FinalComp() const { return final_comp_; }
    int    PresetMode() const { return preset_mode_; }
    int    SelectedSlot() const { return selected_slot_; }
    int    CopySrc() const { return copy_src_; }
    bool   Copying() const { return copying_.load(); }
    bool   ChompiKeyPressed() const { return chompi_key_pressed_; }
    bool   KeyIsPlaying(int sw_id) { return engine_.IsKeyPlaying(sw_id); }
    const Options &Opts() const { return opts_; }
    Options &MutableOpts() { return opts_; }
    void   OptionsChanged();
    uint32_t NowMs() const { return daisy::System::now_ms; }
    const std::string &LastError() const { return last_error_; }

    /* Munchi additions the surface drives: keyboard octave (Up/Down) and
     * whether pads send their velocity (CHOMPI's keys are fixed at 127). */
    int   key_transpose = 0;
    float key_velocity  = 127.f; // < 0: pads send their velocity
    /** The velocity the next key press plays at, when key_velocity < 0. */
    void  SetPadVelocity(float v) { pad_velocity_ = key_velocity < 0.f ? v : 127.f; }

    /* ---- harness -------------------------------------------------------- */
    void ScriptCommand(const char *op, const char *a, float b);

    enum PresetModeE
    {
        PM_NONE = 0,
        PM_ERASE_SEL,
        PM_ERASING,
        PM_COPY_SRC,
        PM_COPY_DEST,
        PM_COPYING,
        PM_SAVE_SEL,
        PM_SAVING,
    };

  private:
    void RunEngineBlock();
    void UiTick();

    /* NormalPage */
    void NormalButton(int id, bool rising);
    void NormalEncoder(int enc, int turns, int steps_per_rev);
    void ApplyKnob(int knob, int page); // Draw()'s per-knob fx_->Set*()
    void OpenCubbiSlot(size_t slot);
    void DumpValuePresets();
    void StopVoiceRecording();
    float PitchFromEnc(float v);

    /* MenuPage */
    bool MenuButton(int id, bool rising); // returns handled
    void MenuEncoder(int enc, int turns);
    void MenuFocusGained();
    bool MenuIsClosable();
    void MenuSetVoiceSlot(size_t slot);

    void QueueCopy(const CopyRequest &req);
    void CopyFinished();

    void SendNoteOn(int note, int vel);
    void SendNoteOff(int note);
    void SendCC(int cc, int val);

    /* card worker */
    void WorkerMain();
    void WorkerSeed();
    void WorkerScan();
    bool WorkerLoadOne();
    void WorkerCopy(const CopyRequest &r);
    void WorkerErase(int mode, int bank, int slot);
    void WorkerPresetsWrite();
    void WorkerRetire(Sample *s);
    void WorkerFreeRetired(bool force);
    std::string SlotPath(int mode, int bank, int slot) const;

    daisy::Engine    engine_;
    daisysp::Reverb *reverb_ = nullptr;
    chompi::InterpolatedDelayLine::AudioSample *del_mem_ = nullptr;
    daisy::RamBufferMemory loop_buff_, chompi_buff_;
    SampleStore      store_;

    /* 48k <-> 44.1k */
    StreamResampler up_, down_;
    static constexpr int kEngineBlock = 48; // libDaisy's default block size
    float    in48_[4][kEngineBlock];
    float    out48_[4][kEngineBlock];
    int      in48_count_ = 0;
    float    out44_l_[1024], out44_r_[1024];
    int      out44_count_ = 0;
    uint32_t block_frac_  = 0; // engine samples since last ms tick

    /* ui.h / NormalPage / MenuPage state */
    float   enc_values_[3][6];
    int     knob_page_[6] = {0, 0, 0, 0, 0, 0};
    bool    switch_state_       = true;
    bool    chompi_key_pressed_ = false;
    int     midi_channel_       = 0;
    bool    quantized_pitch_normal_ = false; // NormalPage: !ps_quant
    bool    menu_active_        = false;
    int     preset_mode_        = PM_NONE;
    uint8_t selected_slot_ = daisy::kSlotNone, ss_bank_ = daisy::kSlotNone;
    int     ss_mode_ = 2;
    uint8_t copy_src_ = daisy::kSlotNone, cs_bank_ = daisy::kSlotNone;
    int     cs_mode_ = 2;
    uint32_t blink_startt_ = 0;
    bool    menu_chompi_pressed_ = false;
    float   delay_time_ = .5f, resonance_ = 0.f, warble_ = 0.f, final_comp_ = 0.f;
    bool    key_cc_[2] = {false, false};
    uint32_t init_time_ = 0;
    bool    init_ignore_ = true;

    float   pad_velocity_ = 127.f;
    Options opts_;
    Presets presets_;
    std::string last_error_;

    /* MIDI out ring (SPI thread only) */
    uint8_t  midi_out_[64][3];
    int      midi_out_r_ = 0, midi_out_w_ = 0;

    /* copier handshake */
    std::atomic<bool> copying_{false};
    std::atomic<bool> copy_done_{false};
    CopyRequest       pending_copy_;
    std::atomic<bool> copy_posted_{false};

    /* options write handshake */
    std::atomic<bool> options_ready_{false};
    std::string       options_snapshot_;
    void              WorkerOptionsWrite();

    /* presets write handshake */
    std::atomic<bool> presets_want_{false};
    std::atomic<bool> presets_ready_{false};
    std::string       presets_snapshot_;
    uint32_t          presets_check_t_ = 0;

    /* card worker */
    std::thread       worker_;
    std::atomic<bool> worker_run_{false};
    std::string       card_dir_, factory_dir_;
    std::atomic<int>  loaded_count_{0}, total_count_{0};
    std::atomic<bool> card_ready_{false};
    std::atomic<bool> seeding_{false};
    std::atomic<bool> presets_known_{false};
    std::atomic<Presets *> loaded_presets_{nullptr};
    std::atomic<Presets *> adopted_presets_{nullptr};
    std::atomic<int>  want_mode_{0}, want_bank_{0}, want_slot_{15};
    std::vector<Sample *> retired_;
};

} // namespace munchi
