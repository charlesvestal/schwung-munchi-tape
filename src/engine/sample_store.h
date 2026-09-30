/* sample_store.h -- the card's samples, resident in RAM.
 *
 * TAPE streamed every sample from the SD card through a per-voice FIFO. Munchi
 * owns the whole device and has the RAM to keep the card resident, so samples
 * are loaded by the card worker (card.cpp) and published here by pointer.
 *
 * Threading: the engine (audio thread) only ever Acquire()s and Release()s.
 * The worker Publish()es. A replaced sample is retired, not freed: it is freed
 * once no voice holds a reference AND the engine has finished at least one
 * block since the swap, which closes the window between an engine load of the
 * old pointer and its reference increment.
 */
#pragma once
#include <atomic>
#include <stdint.h>
#include "tape/RamBuffer.h"

namespace munchi
{

static constexpr int kModes = 2;  // JAMMI (Keys), CUBBI (Kit)
static constexpr int kBanks = 5;  // a..e
static constexpr int kSlots = 14; // 1..14 ; slot 15 is the RAM buffer

struct Sample
{
    daisy::RamBufferMemory mem; // int16 stereo interleaved, 48 kHz
    std::atomic<int>       refs{0};
    uint64_t               retired_at_block = 0;
};

class SampleStore
{
  public:
    SampleStore()
    {
        for(int m = 0; m < kModes; m++)
            for(int b = 0; b < kBanks; b++)
                for(int s = 0; s < kSlots; s++)
                {
                    slot_[m][b][s].store(nullptr);
                    exists_[m][b][s].store(false);
                }
    }

    static bool Valid(int m, int b, int s)
    {
        return m >= 0 && m < kModes && b >= 0 && b < kBanks && s >= 1
               && s <= kSlots;
    }

    /** Audio thread. slot is 1-based like the firmware. */
    Sample *Acquire(int m, int b, int s)
    {
        if(!Valid(m, b, s))
            return nullptr;
        Sample *p = slot_[m][b][s - 1].load(std::memory_order_acquire);
        if(p)
            p->refs.fetch_add(1, std::memory_order_acq_rel);
        return p;
    }

    static void Release(Sample *p)
    {
        if(p)
            p->refs.fetch_sub(1, std::memory_order_acq_rel);
    }

    /** "Is there a file for this slot on the card" -- the firmware's
     *  file_exists[][][] table, which it also only updated from the SD side. */
    bool Exists(int m, int b, int s) const
    {
        if(!Valid(m, b, s))
            return false;
        return exists_[m][b][s - 1].load(std::memory_order_acquire);
    }

    bool Loaded(int m, int b, int s) const
    {
        if(!Valid(m, b, s))
            return false;
        return slot_[m][b][s - 1].load(std::memory_order_acquire) != nullptr;
    }

    void SetExists(int m, int b, int s, bool e)
    {
        if(Valid(m, b, s))
            exists_[m][b][s - 1].store(e, std::memory_order_release);
    }

    /** Worker thread. Returns the previous sample (to be retired). */
    Sample *Publish(int m, int b, int s, Sample *p)
    {
        if(!Valid(m, b, s))
            return p;
        return slot_[m][b][s - 1].exchange(p, std::memory_order_acq_rel);
    }

    /** Engine block counter; the retire rule keys on it. */
    std::atomic<uint64_t> engine_blocks{0};

  private:
    std::atomic<Sample *> slot_[kModes][kBanks][kSlots];
    std::atomic<bool>     exists_[kModes][kBanks][kSlots];
};

} // namespace munchi
