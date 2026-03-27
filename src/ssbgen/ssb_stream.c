#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "ssb_gen.h"

static volatile int running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

#define BLOCK_SIZE 1024
#define AGC_TARGET 0.8f
#define AGC_ATTACK 0.1f
#define AGC_DECAY  0.0001f

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
 * a small carry-over buffer and processed as PCM. This is called
 * each time the loop in the shell script re-cats the file. */
static int skip_wav_header(int16_t *carry, int *carry_count) {
    unsigned char hdr[4];
    *carry_count = 0;
    if (fread(hdr, 1, 4, stdin) != 4) return -1;
    if (memcmp(hdr, "RIFF", 4) != 0) {
        /* Not a WAV — treat these 4 bytes as PCM samples */
        memcpy(carry, hdr, 4);
        *carry_count = 2; /* 4 bytes = 2 int16 samples */
        return 0;
    }
    /* Read rest of the minimal header: bytes 4..11 */
    unsigned char buf[8];
    if (fread(buf, 1, 8, stdin) != 8) return -1;
    /* Now at offset 12. Search for "data" sub-chunk */
    while (1) {
        unsigned char chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, stdin) != 8) return -1;
        uint32_t chunk_size = (uint32_t)chunk_hdr[4]
            | ((uint32_t)chunk_hdr[5] << 8)
            | ((uint32_t)chunk_hdr[6] << 16)
            | ((uint32_t)chunk_hdr[7] << 24);
        if (memcmp(chunk_hdr, "data", 4) == 0)
            return 0; /* stdin now points to PCM data */
        /* Skip unknown chunk */
        if (fseek(stdin, chunk_size, SEEK_CUR) != 0) {
            /* stdin is a pipe, can't seek — consume bytes */
            unsigned char discard[256];
            while (chunk_size > 0) {
                size_t toread = chunk_size < sizeof(discard) ? chunk_size : sizeof(discard);
                if (fread(discard, 1, toread, stdin) != toread) return -1;
                chunk_size -= toread;
            }
        }
    }
}

int main(void) {
    int16_t inbuf[BLOCK_SIZE];
    float outbuf[BLOCK_SIZE * 2];
    int n, i;
    float env = 0.0001f;
    int16_t carry[2];
    int carry_count = 0;
    int need_header = 1;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGPIPE, handle_signal);

    ssb_init(0);

    while (running) {
        /* At the start and after each EOF (loop restart), look for WAV header */
        if (need_header) {
            if (skip_wav_header(carry, &carry_count) < 0) break;
            need_header = 0;
            /* Process carry-over samples from non-WAV detection */
            if (carry_count > 0) {
                for (i = 0; i < carry_count; i++) {
                    float sample = (float)carry[i] / 32768.0f;
                    float I, Q;
                    ssb(sample, MODULE_SSB_USB, &I, &Q);
                    float mag = sqrtf(I * I + Q * Q);
                    if (mag > env)
                        env = env + AGC_ATTACK * (mag - env);
                    else
                        env = env + AGC_DECAY * (mag - env);
                    float gain = (env > 1e-6f) ? AGC_TARGET / env : 1.0f;
                    outbuf[i * 2]     = I * gain;
                    outbuf[i * 2 + 1] = Q * gain;
                }
                if (write_all(STDOUT_FILENO, outbuf, carry_count * 2 * sizeof(float)) < 0)
                    break;
            }
        }
        n = fread(inbuf, sizeof(int16_t), BLOCK_SIZE, stdin);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            float sample = (float)inbuf[i] / 32768.0f;
            float I, Q;
            ssb(sample, MODULE_SSB_USB, &I, &Q);

            /* Fast AGC: track peak magnitude, normalize output */
            float mag = sqrtf(I * I + Q * Q);
            if (mag > env)
                env = env + AGC_ATTACK * (mag - env);
            else
                env = env + AGC_DECAY * (mag - env);

            float gain = (env > 1e-6f) ? AGC_TARGET / env : 1.0f;
            outbuf[i * 2]     = I * gain;
            outbuf[i * 2 + 1] = Q * gain;
        }

        if (write_all(STDOUT_FILENO, outbuf, n * 2 * sizeof(float)) < 0)
            break;

        if (n < BLOCK_SIZE) {
            /* Short read = EOF from one iteration of while/cat loop.
             * Next bytes will be a new copy of the file (with header). */
            need_header = 1;
        }
    }

    return 0;
}
