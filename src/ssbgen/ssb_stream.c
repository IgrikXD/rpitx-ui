#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static volatile int running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

#define BLOCK_SIZE 1024
#define AGC_TARGET 0.8f
#define AGC_ATTACK 0.1f
#define AGC_DECAY  0.001f
#define SAMPLE_RATE 48000.0f

/* ---- Biquad IIR filter ---- */
struct biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
};

static void biquad_init_hpf(struct biquad *bq, float fc, float fs) {
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float alpha = sinf(w0) / (2.0f * 0.7071f);
    float cos_w0 = cosf(w0);
    float a0 = 1.0f + alpha;
    bq->b0 =  (1.0f + cos_w0) / 2.0f / a0;
    bq->b1 = -(1.0f + cos_w0) / a0;
    bq->b2 =  (1.0f + cos_w0) / 2.0f / a0;
    bq->a1 = -2.0f * cos_w0 / a0;
    bq->a2 = (1.0f - alpha) / a0;
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0.0f;
}

static void biquad_init_lpf(struct biquad *bq, float fc, float fs) {
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float alpha = sinf(w0) / (2.0f * 0.7071f);
    float cos_w0 = cosf(w0);
    float a0 = 1.0f + alpha;
    bq->b0 = (1.0f - cos_w0) / 2.0f / a0;
    bq->b1 = (1.0f - cos_w0) / a0;
    bq->b2 = (1.0f - cos_w0) / 2.0f / a0;
    bq->a1 = -2.0f * cos_w0 / a0;
    bq->a2 = (1.0f - alpha) / a0;
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0.0f;
}

static float biquad_process(struct biquad *bq, float in) {
    float out = bq->b0 * in + bq->b1 * bq->x1 + bq->b2 * bq->x2
                             - bq->a1 * bq->y1 - bq->a2 * bq->y2;
    bq->x2 = bq->x1; bq->x1 = in;
    bq->y2 = bq->y1; bq->y1 = out;
    return out;
}

/* ---- Hilbert transform FIR (255 taps, Blackman window) ---- */
#define HILBERT_TAPS 255
#define HILBERT_M    ((HILBERT_TAPS - 1) / 2)  /* 127 */

static float h_coeffs[HILBERT_TAPS];
static float h_line[HILBERT_TAPS];
static float d_line[HILBERT_TAPS]; /* delay line for I channel */
static int   fir_pos = 0;

static void hilbert_init(void) {
    int n;
    for (n = 0; n < HILBERT_TAPS; n++) {
        int k = n - HILBERT_M;
        if (k == 0 || (k & 1) == 0) {
            h_coeffs[n] = 0.0f;
        } else {
            /* Blackman window */
            float w = 0.42f - 0.5f * cosf(2.0f * (float)M_PI * n / (HILBERT_TAPS - 1))
                            + 0.08f * cosf(4.0f * (float)M_PI * n / (HILBERT_TAPS - 1));
            h_coeffs[n] = (2.0f / ((float)M_PI * k)) * w;
        }
    }
    memset(h_line, 0, sizeof(h_line));
    memset(d_line, 0, sizeof(d_line));
    fir_pos = 0;
}

/* Process one sample: returns Hilbert-transformed Q and delayed I */
static void hilbert_process(float in, float *out_I, float *out_Q) {
    int i, idx;
    float q_acc = 0.0f;

    h_line[fir_pos] = in;
    d_line[fir_pos] = in;

    /* FIR convolution for Hilbert (Q channel) */
    idx = fir_pos;
    for (i = 0; i < HILBERT_TAPS; i++) {
        q_acc += h_coeffs[i] * h_line[idx];
        if (--idx < 0) idx = HILBERT_TAPS - 1;
    }

    /* I channel: delayed by HILBERT_M samples */
    idx = fir_pos - HILBERT_M;
    if (idx < 0) idx += HILBERT_TAPS;
    *out_I = d_line[idx];
    *out_Q = q_acc;

    fir_pos = (fir_pos + 1) % HILBERT_TAPS;
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w <= 0) return -1;
        p += w;
        len -= w;
    }
    return 0;
}

/* Skip a WAV header if present. Detects "RIFF" magic, reads the
 * header to find the "data" chunk and positions stdin right after it.
 * If stdin does not start with "RIFF", the bytes read are kept in
 * a small carry-over buffer and processed as PCM. */
static int skip_wav_header(int16_t *carry, int *carry_count) {
    unsigned char hdr[4];
    *carry_count = 0;
    if (fread(hdr, 1, 4, stdin) != 4) return -1;
    if (memcmp(hdr, "RIFF", 4) != 0) {
        memcpy(carry, hdr, 4);
        *carry_count = 2;
        return 0;
    }
    unsigned char buf[8];
    if (fread(buf, 1, 8, stdin) != 8) return -1;
    while (1) {
        unsigned char chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, stdin) != 8) return -1;
        uint32_t chunk_size = (uint32_t)chunk_hdr[4]
            | ((uint32_t)chunk_hdr[5] << 8)
            | ((uint32_t)chunk_hdr[6] << 16)
            | ((uint32_t)chunk_hdr[7] << 24);
        if (memcmp(chunk_hdr, "data", 4) == 0)
            return 0;
        if (fseek(stdin, chunk_size, SEEK_CUR) != 0) {
            unsigned char discard[256];
            while (chunk_size > 0) {
                size_t toread = chunk_size < sizeof(discard) ? chunk_size : sizeof(discard);
                if (fread(discard, 1, toread, stdin) != toread) return -1;
                chunk_size -= toread;
            }
        }
    }
}

static void process_sample(float sample, float *env,
                           struct biquad *hpf, struct biquad *lpf,
                           float *out_I, float *out_Q) {
    /* Bandpass 300-3000 Hz */
    float filtered = biquad_process(hpf, sample);
    filtered = biquad_process(lpf, filtered);

    /* Hilbert transform -> analytic signal (I + jQ = USB) */
    float I, Q;
    hilbert_process(filtered, &I, &Q);

    /* Fast AGC */
    float mag = sqrtf(I * I + Q * Q);
    if (mag > *env)
        *env = *env + AGC_ATTACK * (mag - *env);
    else
        *env = *env + AGC_DECAY * (mag - *env);

    float gain = (*env > 1e-6f) ? AGC_TARGET / *env : 1.0f;
    *out_I = I * gain;
    *out_Q = Q * gain;
}

int main(void) {
    int16_t inbuf[BLOCK_SIZE];
    float outbuf[BLOCK_SIZE * 2];
    int n, i;
    float env = 0.0001f;
    int16_t carry[2];
    int carry_count = 0;
    int need_header = 1;

    struct biquad hpf, lpf;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGPIPE, handle_signal);

    hilbert_init();
    biquad_init_hpf(&hpf, 300.0f, SAMPLE_RATE);
    biquad_init_lpf(&lpf, 3000.0f, SAMPLE_RATE);

    while (running) {
        if (need_header) {
            if (skip_wav_header(carry, &carry_count) < 0) break;
            need_header = 0;
            if (carry_count > 0) {
                for (i = 0; i < carry_count; i++) {
                    float sample = (float)carry[i] / 32768.0f;
                    process_sample(sample, &env, &hpf, &lpf,
                                   &outbuf[i * 2], &outbuf[i * 2 + 1]);
                }
                if (write_all(STDOUT_FILENO, outbuf, carry_count * 2 * sizeof(float)) < 0)
                    break;
            }
        }
        n = fread(inbuf, sizeof(int16_t), BLOCK_SIZE, stdin);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            float sample = (float)inbuf[i] / 32768.0f;
            process_sample(sample, &env, &hpf, &lpf,
                           &outbuf[i * 2], &outbuf[i * 2 + 1]);
        }

        if (write_all(STDOUT_FILENO, outbuf, n * 2 * sizeof(float)) < 0)
            break;

        if (n < BLOCK_SIZE) {
            need_header = 1;
        }
    }

    return 0;
}
