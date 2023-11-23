#ifndef _VIBEXEC_VIBEOMATIC_H_
#define _VIBEXEC_VIBEOMATIC_H_

#include <kissfft/kiss_fft.h>
#include <time.h>
#include "scheduler.h"

struct vibexec_vibeomatic_session {
    const struct vibexec_schedulable_parameters *parameters;
    unsigned long sample_window_size;

    struct {
        double nanoseconds_per_window;
        unsigned long window_size_in_bytes;

        kiss_fft_cpx *last_window_out;
        kiss_fft_cpx *current_window_in;
        kiss_fft_cpx *current_window_out;

        kiss_fft_cfg fft_config;

        /* Score buffer */

        double *score_buffer;
        unsigned long score_buffer_capacity;
        unsigned long score_buffer_limit;
        struct timespec score_buffer_offset;
        unsigned long score_buffer_offset_index;
    } cache;
};

void vibexec_vibeomatic_analyze(
    struct vibexec_vibeomatic_session *session,
    void *buffer,
    unsigned long buffer_size
);

void vibexec_vibeomatic_cleanup(struct vibexec_vibeomatic_session *session);
double vibexec_vibeomatic_drop_and_score(
    struct vibexec_vibeomatic_session *session,
    struct timespec *offset
);

int vibexec_vibeomatic_initialize(
    struct vibexec_vibeomatic_session *session,
    const struct vibexec_schedulable_parameters *parameters,
    unsigned long sample_window_size
);

/* Inline functions. */

static inline void _compute_difference(
    struct timespec *dst,
    const struct timespec *minuend,
    const struct timespec *subtrahend
) {
    dst->tv_sec = minuend->tv_sec - subtrahend->tv_sec;
    dst->tv_nsec = minuend->tv_nsec;

    if (subtrahend->tv_nsec < dst->tv_nsec) {
        dst->tv_nsec -= subtrahend->tv_nsec;
    } else {
        dst->tv_sec--;
        dst->tv_nsec = 1000000000 - (subtrahend->tv_nsec - dst->tv_nsec);
    }
}

#endif
