/* SampleReader.h -- CHOMPI TAPE's voice, ported to RAM-resident samples.
 *
 * The firmware original is kept beside this file as upstream/SampleReader.h.
 * It had two read paths: a streaming one for card samples (an SD FIFO with
 * async OPEN/SEEK/READ requests, a separate "_double" file for playback above
 * 1.5x) and a RAM one for the CHOMPI buffer (slot 15). Card samples are
 * resident here, so EVERY source goes through the firmware's RAM path, line
 * for line: the same ADSR, the same click envelope on retrigger, the same
 * 150-frame loop fade, the same linear interpolation.
 *
 * What the streaming path contributed that is kept:
 *   - the 2x read above 1.5x speed. TAPE's "_double" files are plain 2:1
 *     decimations (FileCopier writes every other frame, no filter), read at
 *     half the increment. Here the voice reads every other frame of the
 *     resident sample instead, which is the same data without the file.
 *
 * What it contributed that is not: FIFO under-run holds, seek/open request
 * bookkeeping and the SD read-ahead cache. Those were artefacts of the card,
 * not of the instrument.
 *
 * Naming is kept from the firmware so the engine port reads like the
 * original: `using_ram` still means "this voice plays the CHOMPI buffer".
 */
#pragma once
#include "../daisy_compat.h"
#include "RamBuffer.h"
#include "../sample_store.h"

namespace daisy
{

static const uint16_t kMaxClickSamps          = 300; // 6ish ms
static constexpr size_t kMaxFileStreamingSamps = 8192;
static const uint16_t kMinLoopLen             = kMaxFileStreamingSamps / 2;

class FileSampleReader
{
  public:
    void Init(float sr, RamBufferMemory *chompi_buff, munchi::SampleStore *store)
    {
        sr_          = sr;
        store_       = store;
        chompi_mem_  = chompi_buff;
        sample_      = nullptr;
        file_m_      = -1;
        file_b_      = -1;
        file_s_      = -1;
        pending_open_ = false;
        ram_buff.Attach(&empty_mem_);
        empty_mem_.mem      = &empty_data_[0];
        empty_mem_.length   = 0;
        empty_mem_.capacity = 2;

        env_.Init(sr);
        env_.SetSustainLevel(1.f);

        RestoreDefaults();

        using_ram = using_ram_target = false;
        AttachSource();
    }

    void SetVelocity(float vel) { velocity = vel * 0.00787401f; }

    void SetAutoLoop(bool loop) { auto_loop = loop; }
    void ToggleAutoLoop() { auto_loop = !auto_loop; }
    bool GetAutoLoop() { return auto_loop; }

    void SetSustainActive(bool sus) { sustain = sus; }
    bool GetSustainActive() { return sustain; }
    void ToggleSustainActive() { sustain = !sustain; }

    inline void SetUsingRam(bool use, bool click)
    {
        using_ram_target = use;
        if(click)
        {
            CalculateClickSamps();
            deferred_trig = env_gate_ || cubbi_mode;
        }

        note_changed = true;

        if(!env_.IsRunning())
            SwitchSource();
    }

    inline void SetStartPointForce(float val) { fstart_ = val; }

    /* The firmware's RAM branch of SetStartPoint: points are fractions of the
     * source, rejected when the loop would drop under kMinLoopLen frames. */
    bool SetStartPoint(float val)
    {
        uint32_t new_start_point = val * ram_buff.GetSize();
        new_start_point -= (new_start_point % 2);
        new_start_point /= 2;

        uint32_t end_point = fend_ * ram_buff.GetSize();
        end_point -= (end_point % 2);
        end_point /= 2;

        bool ret = false;
        if(end_point - new_start_point < kMinLoopLen
           || new_start_point >= end_point)
        {
            // loop would be too short
        }
        else
        {
            fstart_ = val;
            ret     = true;
        }
        return ret;
    }

    inline uint32_t GetStartPoint()
    {
        uint32_t start_point = fstart_ * ram_buff.GetSize();
        return start_point - (start_point % 2);
    }

    inline void SetEndPointForce(float val) { fend_ = val; }

    bool SetEndPoint(float val)
    {
        uint32_t new_end_point = val * ram_buff.GetSize();
        new_end_point -= (new_end_point % 2);
        new_end_point /= 2;

        uint32_t start_point = fstart_ * ram_buff.GetSize();
        start_point -= (start_point % 2);
        start_point /= 2;

        bool ret = false;
        if(new_end_point - start_point < kMinLoopLen
           || start_point >= new_end_point)
        {
            // loop would be too short
        }
        else
        {
            fend_ = val;
            ret   = true;
        }
        return ret;
    }

    inline uint32_t GetEndPoint()
    {
        uint32_t end_point = fend_ * ram_buff.GetSize();
        return end_point - (end_point % 2);
    }

    /* Munchi: the start/end fractions, for the display. */
    inline float GetStartFrac() const { return fstart_; }
    inline float GetEndFrac() const { return fend_; }

    void ResetEnv()
    {
        env_.Init(sr_);
        env_.SetSustainLevel(1.f);
        env_.SetAttackTime(att_time_, 0.f);
        env_.SetReleaseTime(rel_time_);
        env_gate_ = false;
    }

    /** Sustain, autoloop, pitch, reverse, startpoint, endpoint, attack,
     *  decay, gain, and pan */
    void RestoreDefaults()
    {
        auto_loop = true;
        sustain   = true;

        SetGlobalPitch(1.f);
        SetVarispeed(1.f);
        SetReverse(false);

        // SetStartPoint/SetEndPoint validate against the loaded source and
        // refuse an empty one; the firmware's defaults are 0 and 1.
        fstart_ = 0.f;
        fend_   = 1.f;

        SetAttack(.01f);
        SetDecay(.01f);

        gain_ = gain_target_ = .704f;
        pan_raw_             = .5f;
        pan_l_ = pan_l_target_ = 1.f;
        pan_r_ = pan_r_target_ = 1.f;
    }

    /** Mapping: 0-1 = 0x-2x  */
    void SetGain(float val)
    {
        gain_target_ = 2.f * val * val + .01f;
        if(!IsPlaying())
            gain_ = gain_target_;
    }

    /** Mapping: 0-1 = L-R with .5 being center */
    inline void SetPan(float val)
    {
        pan_raw_      = daisysp::fclamp(val, 0.f, 1.f);
        pan_r_target_ = fminf(2.f * pan_raw_, 1.f);
        pan_l_target_ = fminf(2.f - 2.f * pan_raw_, 1.f);
        if(!IsPlaying())
        {
            pan_l_ = pan_l_target_;
            pan_r_ = pan_r_target_;
        }
    }

    inline float GetPan() { return pan_raw_; }

    void SetAttack(float val)
    {
        att_time_ = val * 20.f + .001f;
        env_.SetAttackTime(att_time_, 0.f);
    }

    void SetDecay(float val)
    {
        rel_time_ = val * 4.f + .001f;
        env_.SetReleaseTime(rel_time_);
    }

    void JumpTo(uint32_t pos = 0) { ram_buff.SetReadHead(pos); }

    void JumpToStart()
    {
        if(reverse_)
            JumpTo(GetEndPoint());
        else
            JumpTo(GetStartPoint());
    }

    /** OpenFile(name, name_double, reset) in the firmware. A slot is named by
     *  (mode, bank, slot) here rather than by file name; the "_double" file is
     *  implied (see header). */
    void OpenFile(int mode, int bank, int slot, bool reset)
    {
        file_m_       = mode;
        file_b_       = bank;
        file_s_       = slot;
        pending_open_ = true;

        if(reset)
        {
            if(env_.IsRunning())
            {
                CalculateClickSamps();
                deferred_jump = true;
            }
            else
            {
                click_env   = 1.f;
                click_dec   = .002f;
                click_clear = 0;
            }
        }
        else
        {
            click_env   = 1.f;
            click_dec   = .002f;
            click_clear = 0;
        }

        // Not playing: nothing to fade, open now.
        if(!env_.IsRunning())
            CompleteOpen();
    }

    void CloseFile()
    {
        if(!using_ram)
            ram_buff.Attach(&empty_mem_);
        munchi::SampleStore::Release(sample_);
        sample_       = nullptr;
        file_m_       = -1;
        pending_open_ = false;
    }

    void PopStereoSamps(float *l, float *r)
    {
        float env_sig = env_.Process(env_gate_);

        // A slot opened while this voice was sounding waits for the click
        // fade (the firmware waited for the SD open to complete).
        if(pending_open_ && (click_env <= 0.f || !env_.IsRunning()))
            CompleteOpen();
        // A card sample that was still loading when it was opened.
        else if(!using_ram && !sample_ && file_m_ >= 0)
            CompleteOpen();

        if(!pending_open_ && deferred_jump)
        {
            deferred_jump = false;
            JumpToStart();
        }

        if(env_.GetCurrentSegment() == daisysp::ADSR_SEG_DECAY && !sustain)
            env_gate_ = false;

        if(note_changed || pitch_changed)
        {
            CheckDoubleSpeed();
            note_changed = pitch_changed = false;
        }

        daisysp::fonepole(gain_, gain_target_, .001f);
        daisysp::fonepole(pan_l_, pan_l_target_, .001f);
        daisysp::fonepole(pan_r_, pan_r_target_, .001f);

        if(pan_l_ < .01f && pan_l_target_ < .01f)
            pan_l_ = 0.f;
        if(pan_r_ < .01f && pan_r_target_ < .01f)
            pan_r_ = 0.f;

        if(env_sig < .001f && !deferred_trig)
        {
            *l = last_l * rev_env;
            *r = last_r * rev_env;
        }
        else
        {
            float inc = varispeed_factor * global_pitch;
            if(double_speed)
                inc *= 0.5f;

            rpos_frac_ += inc;
            if(rpos_frac_ >= 1.f)
            {
                uint32_t samp_stride = (uint32_t)rpos_frac_;
                rpos_frac_ -= samp_stride;

                for(uint32_t i = 0; i < samp_stride; i++)
                {
                    // click envelope (for when the voice interrupts itself)
                    click_env += click_dec;

                    if(click_dec < 0.f)
                    {
                        if(click_env <= 0.f || (env_sig < .01f && deferred_trig))
                        {
                            click_dec = .004f;

                            if(pending_open_)
                                CompleteOpen();

                            if(deferred_trig)
                            {
                                env_.Retrigger(false);
                                env_gate_     = true;
                                deferred_trig = false;
                            }

                            click_clear = 0;
                            JumpToStart();

                            varispeed_counter = 0;
                            varispeed_factor  = varispeed_target;
                        }
                    }
                    click_env = daisysp::fclamp(click_env, 0.f, 1.f);
                    click_clear -= 2;
                    if(click_clear <= 0)
                    {
                        SwitchSource();
                        click_clear = 0;
                    }

                    // loop envelope (for when the voice loops over the end)
                    if(loop_env_delay == 0)
                    {
                        loop_env += loop_env_dec;
                        if(loop_env < 0.f)
                        {
                            loop_env     = 0.f;
                            loop_env_dec = fabsf(loop_env_dec);

                            if(auto_loop)
                                JumpToStart();
                        }
                    }
                    else
                    {
                        loop_env_delay--;
                    }
                    loop_env = daisysp::fclamp(loop_env, 0.f, 1.f);

                    rev_env += rev_env_dec;
                    if(rev_env < 0.f)
                        rev_env_dec = fabsf(rev_env_dec);
                    rev_env  = daisysp::fclamp(rev_env, 0.f, 1.f);
                    rev_wait = rev_wait >= 2 ? 2 : rev_wait + 1;

                    varispeed_counter
                        = varispeed_counter == 0 ? 0 : varispeed_counter - 1;
                    if(varispeed_counter == 0)
                        varispeed_factor = varispeed_target;

                    // update samples, B become A, and A get read
                    read_left_a_  = read_left_b_;
                    read_right_a_ = read_right_b_;
                    ReadFrame(&read_left_b_, &read_right_b_);
                }
            }

            if(rev_wait == 2)
            {
                /** linear interpolation */
                float tl = read_left_a_ + (read_left_b_ - read_left_a_) * rpos_frac_;
                float tr = read_right_a_
                           + (read_right_b_ - read_right_a_) * rpos_frac_;

                *l = s162f(tl) * env_sig * click_env * loop_env * rev_env
                     * velocity * gain_ * pan_l_;
                *r = s162f(tr) * env_sig * click_env * loop_env * rev_env
                     * velocity * gain_ * pan_r_;
                last_l = *l;
                last_r = *r;
            }
            else
            {
                *l = last_l * rev_env;
                *r = last_r * rev_env;
            }
        }

        // Stop playing if we're not auto-looping
        if(OTE() && !auto_loop && env_.IsRunning())
        {
            ResetEnv();
        }
        else if((reverse_ && ram_buff.GetReadHead() <= GetStartPoint() + kMaxClickSamps)
                || (!reverse_
                    && ram_buff.GetReadHead() + kMaxClickSamps >= GetEndPoint())
                || ram_buff.ReadLoop(reverse_))
        {
            loop_env_dec = -2.f / kMaxClickSamps;
        }
    }

    bool OTE()
    {
        if(deferred_jump)
            return false;
        if(reverse_)
            return ram_buff.GetReadHead() <= GetStartPoint();
        return ram_buff.GetReadHead() >= GetEndPoint();
    }

    /** Quick hacks to fix a wrong voice bug (kept: the engine reads them) */
    bool cubbi_mode = false;
    void SetCubbiMode(bool mode) { cubbi_mode = mode; }
    bool GetCubbiMode() { return cubbi_mode; }

    uint8_t bank = 0;
    void    SetBank(uint8_t b) { bank = b; }
    uint8_t GetBank() { return bank; }

    uint8_t slot = 15;
    void    SetSlot(uint8_t s) { slot = s; }
    uint8_t GetSlot() { return slot; }

    inline void SetVarispeed(float speed)
    {
        if(varispeed_factor == 0.f || !env_.IsRunning())
        {
            varispeed_factor  = speed;
            varispeed_target  = speed;
            varispeed_counter = 0;
        }
        else
        {
            varispeed_target = speed;
            // The firmware waited out the FIFO's contents (up to 4096 frames)
            // before the new speed took effect; a resident sample has no FIFO,
            // so the change lands on the next frame.
            varispeed_counter = 1;
        }
        note_changed = true;
    }

    inline void SetGlobalPitch(float speed)
    {
        global_pitch  = speed;
        pitch_changed = true;
    }
    inline float GetGlobalPitch() { return global_pitch; }

    void SetReverse(bool rev)
    {
        if(reverse_ != rev)
            reverse_ = rev;
    }
    inline bool GetReverse() { return reverse_; }

    void CalculateClickSamps()
    {
        deferred_trig = true;
        click_dec     = .002f;
        click_clear   = 0;

        if(env_.IsRunning())
        {
            click_dec   = -2.f / kMaxClickSamps;
            click_clear = kMaxClickSamps;
        }
    }

    bool deferred_jump = false;
    bool deferred_trig = false;

    void StartPlaying()
    {
        const bool is_running = env_.IsRunning();

        if(is_running && click_clear == 0)
        {
            CalculateClickSamps();
        }
        else if(!is_running)
        {
            click_env   = 0.f;
            click_dec   = .01f;
            click_clear = 0;
            rev_wait    = 0;
            last_l = last_r = 0.f;

            env_.Retrigger(false);
            deferred_jump = true;
            env_gate_     = true;
        }

        // Hack fixes issue with short samples being silent on second button press
        loop_env_delay = 0;
        if(loop_env_dec < 0.f)
            loop_env_dec *= -1.f;
    }

    inline void StopPlaying()
    {
        env_gate_     = false;
        deferred_trig = false;
    }

    inline void Choke()
    {
        SetDecay(.01f);
        env_gate_ = false;
        CalculateClickSamps();
        deferred_trig = false;
    }

    inline bool IsPlaying() { return env_.IsRunning() || env_gate_; }

    /* The firmware pre-read the first block of the sample while idle so a
     * note could start without waiting on the card. Nothing to do here. */
    inline void CacheSamples() {}

    inline size_t GetSize() { return ram_buff.GetSize(); }

    /* Munchi: play position as a fraction of the source, for the display. */
    float GetPositionFrac()
    {
        size_t n = ram_buff.GetSize();
        return n ? (float)ram_buff.GetReadHead() / (float)n : 0.f;
    }

    bool copy_occurred = false;
    inline void SetCopyOccurred() { copy_occurred = true; }
    inline bool GetCopyOccurred()
    {
        const bool ret = copy_occurred;
        copy_occurred  = false;
        return ret;
    }

  private:
    /* Reads the next frame into b; in 2x mode, reads the even frame and skips
     * the odd one, which is exactly the content of TAPE's "_double" file. */
    inline void ReadFrame(int16_t *bl, int16_t *br)
    {
        if(!double_speed)
        {
            ram_buff.StereoRead(bl, br, reverse_);
            return;
        }
        int16_t xl, xr;
        if(!reverse_)
        {
            ram_buff.StereoRead(bl, br, false);
            ram_buff.StereoRead(&xl, &xr, false);
        }
        else
        {
            ram_buff.StereoRead(&xl, &xr, true);
            ram_buff.StereoRead(bl, br, true);
        }
    }

    /** CheckDoubleSpeed(): a resident sample only needs the head aligned to
     *  the 2x grid (an even frame) when it enters double speed. */
    void CheckDoubleSpeed()
    {
        const bool want = !using_ram && varispeed_target * global_pitch > 1.5f;
        if(want && !double_speed)
        {
            size_t h = ram_buff.GetReadHead();
            ram_buff.SetReadHead(h - (h % 4));
        }
        double_speed = want;
    }

    void CompleteOpen()
    {
        munchi::Sample *p = store_ ? store_->Acquire(file_m_, file_b_, file_s_)
                                   : nullptr;
        if(!p)
        {
            // still loading (or empty): stay pending on the data, not the fade
            if(pending_open_)
            {
                munchi::SampleStore::Release(sample_);
                sample_       = nullptr;
                pending_open_ = false;
                if(!using_ram)
                    ram_buff.Attach(&empty_mem_);
            }
            return;
        }
        munchi::SampleStore::Release(sample_);
        sample_       = p;
        pending_open_ = false;
        if(!using_ram)
            AttachSource();
        FileOpened();
    }

    // call this post file load. recalculates end point and so on
    void FileOpened()
    {
        SetStartPoint(fstart_);
        SetEndPoint(fend_);
    }

    void SwitchSource()
    {
        if(using_ram != using_ram_target)
        {
            using_ram = using_ram_target;
            AttachSource();
        }
    }

    void AttachSource()
    {
        if(using_ram)
            ram_buff.Attach(chompi_mem_);
        else if(sample_)
            ram_buff.Attach(&sample_->mem);
        else
            ram_buff.Attach(&empty_mem_);
    }

    munchi::SampleStore *store_;
    munchi::Sample      *sample_;
    int                  file_m_, file_b_, file_s_;
    bool                 pending_open_;
    RamBufferMemory     *chompi_mem_;
    RamBufferMemory      empty_mem_;
    int16_t              empty_data_[2] = {0, 0};

    bool double_speed = false;
    bool note_changed = false, pitch_changed = false;

    /** Varispeed handling */
    float    varispeed_factor = 1.f, varispeed_target = 1.f;
    float    global_pitch      = 1.f;
    uint32_t varispeed_counter = 0;

    bool    reverse_    = false;
    float   rev_env     = 1.f;
    float   rev_env_dec = .01f;
    uint8_t rev_wait    = 0;

    float gain_, gain_target_;
    float pan_raw_, pan_l_, pan_l_target_, pan_r_, pan_r_target_;

    float rpos_frac_ = 0.f;

    int16_t read_left_a_ = 0, read_left_b_ = 0;
    int16_t read_right_a_ = 0, read_right_b_ = 0;

    bool  env_gate_ = false;
    float click_env = 1.f;
    float click_dec = .01f;

    float  loop_env       = 1.f;
    float  loop_env_dec   = .01f;
    size_t loop_env_delay = 0;

    float last_l = 0.f, last_r = 0.f;

    float          sr_;
    daisysp::Adsr  env_;
    float          att_time_, rel_time_;
    bool           auto_loop, sustain;
    float          fstart_ = 0.f, fend_ = 1.f;
    int            click_clear = 0;
    float          velocity    = 1.f;

    RamBuffer ram_buff;
    bool      using_ram, using_ram_target;
};

} // namespace daisy
