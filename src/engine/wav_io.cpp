#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

namespace munchi
{

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static float read_sample(const uint8_t *p, int bits, int is_float)
{
    if(is_float && bits == 32)
    {
        float f;
        memcpy(&f, p, 4);
        return f;
    }
    if(is_float && bits == 64)
    {
        double d;
        memcpy(&d, p, 8);
        return (float)d;
    }
    switch(bits)
    {
        case 8: return ((int)p[0] - 128) / 128.f;
        case 16: return (int16_t)rd16(p) / 32768.f;
        case 24:
        {
            int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16
                                  | (uint32_t)p[2] << 24);
            return (v >> 8) / 8388608.f;
        }
        case 32: return (int32_t)rd32(p) / 2147483648.f;
    }
    return 0.f;
}

static inline int16_t to_s16(float x)
{
    // f2s16 from libDaisy: clamp to +-1, scale by 32767
    x = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
    return (int16_t)(x * 32767.f);
}

/* Windowed-sinc resampling of a planar float buffer. Only runs for files that
 * are not already 48 kHz, on the worker, once per load. */
static float *resample(const float *in, size_t n_in, double ratio, size_t *n_out)
{
    const int    half = 16;
    const double cutoff = ratio < 1.0 ? ratio : 1.0; // anti-alias when downsampling
    size_t       n = (size_t)floor((double)n_in * ratio);
    float       *out = (float *)malloc((n ? n : 1) * sizeof(float));
    if(!out)
        return nullptr;
    for(size_t i = 0; i < n; i++)
    {
        double pos  = (double)i / ratio;
        long   c    = (long)floor(pos);
        double acc = 0.0, wsum = 0.0;
        for(long k = c - half + 1; k <= c + half; k++)
        {
            if(k < 0 || (size_t)k >= n_in)
                continue;
            double x = pos - (double)k;
            double s = fabs(x) < 1e-9 ? 1.0
                                      : sin(M_PI * x * cutoff) / (M_PI * x * cutoff);
            double w = 0.5 * (1.0 + cos(M_PI * x / (double)half)); // Hann
            double t = s * w;
            acc += in[k] * t;
            wsum += t;
        }
        out[i] = (float)(wsum > 0.0 ? acc / wsum : 0.0);
    }
    *n_out = n;
    return out;
}

int wav_load_48k_stereo(const char *path, int16_t **out, size_t *frames,
                        char *err, size_t err_len)
{
    *out    = nullptr;
    *frames = 0;
    FILE *f = fopen(path, "rb");
    if(!f)
    {
        snprintf(err, err_len, "open failed");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(len < 12)
    {
        fclose(f);
        snprintf(err, err_len, "too short");
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if(!buf || fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        snprintf(err, err_len, "read failed");
        return -1;
    }
    fclose(f);

    if(memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
    {
        free(buf);
        snprintf(err, err_len, "not a WAV");
        return -1;
    }

    int            fmt = 0, ch = 0, bits = 0;
    uint32_t       rate = 0;
    const uint8_t *data = nullptr;
    size_t         data_len = 0;
    size_t         off = 12;
    while(off + 8 <= (size_t)len)
    {
        const uint8_t *ck = buf + off;
        uint32_t       sz = rd32(ck + 4);
        size_t         body = off + 8;
        if(!memcmp(ck, "fmt ", 4) && body + 16 <= (size_t)len)
        {
            fmt  = rd16(buf + body);
            ch   = rd16(buf + body + 2);
            rate = rd32(buf + body + 4);
            bits = rd16(buf + body + 14);
            if(fmt == 0xFFFE && sz >= 26 && body + 26 <= (size_t)len)
                fmt = rd16(buf + body + 24); // WAVE_FORMAT_EXTENSIBLE subformat
        }
        else if(!memcmp(ck, "data", 4))
        {
            data     = buf + body;
            data_len = sz;
            if(body + data_len > (size_t)len)
                data_len = (size_t)len - body;
            break;
        }
        off = body + sz + (sz & 1);
    }

    const int is_float = fmt == 3;
    if(!data || !ch || !rate || !(fmt == 1 || fmt == 3)
       || !(bits == 8 || bits == 16 || bits == 24 || bits == 32
            || (is_float && bits == 64)))
    {
        free(buf);
        snprintf(err, err_len, "unsupported format");
        return -1;
    }

    const size_t bpf = (size_t)(bits / 8) * (size_t)ch;
    size_t       n   = data_len / bpf;
    if(n == 0)
    {
        free(buf);
        snprintf(err, err_len, "empty");
        return -1;
    }

    // Fast path: TAPE's native format
    if(fmt == 1 && bits == 16 && ch == 2 && rate == 48000)
    {
        int16_t *pcm = (int16_t *)malloc(n * 2 * sizeof(int16_t));
        if(!pcm)
        {
            free(buf);
            snprintf(err, err_len, "out of memory");
            return -1;
        }
        memcpy(pcm, data, n * 2 * sizeof(int16_t));
        free(buf);
        *out    = pcm;
        *frames = n;
        return 0;
    }

    float *l = (float *)malloc(n * sizeof(float));
    float *r = (float *)malloc(n * sizeof(float));
    if(!l || !r)
    {
        free(l);
        free(r);
        free(buf);
        snprintf(err, err_len, "out of memory");
        return -1;
    }
    const size_t bps = (size_t)bits / 8;
    for(size_t i = 0; i < n; i++)
    {
        const uint8_t *p = data + i * bpf;
        l[i]             = read_sample(p, bits, is_float);
        r[i]             = ch > 1 ? read_sample(p + bps, bits, is_float) : l[i];
    }
    free(buf);

    if(rate != 48000)
    {
        size_t nl = 0, nr = 0;
        double ratio = 48000.0 / (double)rate;
        float *l2 = resample(l, n, ratio, &nl);
        float *r2 = resample(r, n, ratio, &nr);
        free(l);
        free(r);
        if(!l2 || !r2)
        {
            free(l2);
            free(r2);
            snprintf(err, err_len, "out of memory");
            return -1;
        }
        l = l2;
        r = r2;
        n = nl < nr ? nl : nr;
    }

    int16_t *pcm = (int16_t *)malloc(n * 2 * sizeof(int16_t));
    if(!pcm)
    {
        free(l);
        free(r);
        snprintf(err, err_len, "out of memory");
        return -1;
    }
    for(size_t i = 0; i < n; i++)
    {
        pcm[i * 2]     = to_s16(l[i]);
        pcm[i * 2 + 1] = to_s16(r[i]);
    }
    free(l);
    free(r);
    *out    = pcm;
    *frames = n;
    return 0;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}

int wav_write_48k_stereo(const char *path, const int16_t *pcm, size_t frames)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if(!f)
        return -1;
    uint8_t  h[44];
    uint32_t data_len = (uint32_t)(frames * 4);
    memcpy(h, "RIFF", 4);
    wr32(h + 4, 36 + data_len);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr32(h + 16, 16);
    wr16(h + 20, 1);
    wr16(h + 22, 2);
    wr32(h + 24, 48000);
    wr32(h + 28, 48000 * 4);
    wr16(h + 32, 4);
    wr16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    wr32(h + 40, data_len);
    int ok = fwrite(h, 1, 44, f) == 44
             && fwrite(pcm, 4, frames, f) == frames;
    ok = (fflush(f) == 0) && ok;
    fsync(fileno(f));
    fclose(f);
    if(!ok)
    {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path) == 0 ? 0 : -1;
}

} // namespace munchi
