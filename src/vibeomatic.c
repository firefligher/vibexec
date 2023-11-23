#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vibeomatic.h"

static inline int _is_ge_than(const struct timespec *left, double right);
static inline double _score(
    kiss_fft_cpx *last_window,
    kiss_fft_cpx *current_window,
    unsigned long window_size
);

void vibexec_vibeomatic_analyze(
    struct vibexec_vibeomatic_session *session,
    void *buffer,
    unsigned long buffer_size
) {
    kiss_fft_cpx *wnd_cur_in, *wnd_cur_out, *wnd_last;
    unsigned long buffer_offset;

    /* Aliasing. */

    wnd_last = session->cache.last_window_out;
    wnd_cur_in = session->cache.current_window_in;
    wnd_cur_out = session->cache.current_window_out;

    /* Do the FFT for each complete window and score. */

    for (
        buffer_offset = 0;
        buffer_size - buffer_offset >= session->cache.window_size_in_bytes;
        buffer_offset += session->cache.window_size_in_bytes
    ) {
        int sample;
        double score;

        /* Decode the window and normalize to mono channel. */

        for (sample = 0; sample < session->sample_window_size; sample++) {
            float amplitude = 0.0F;
            int channel;

            for (
                channel = 0;
                channel < session->parameters->channels;
                channel++
            ) {
                switch (session->parameters->sample_format) {
                    case SIGNED_8BIT:
                        amplitude += ((char *) buffer)[
                            buffer_offset +
                            sample * session->parameters->channels +
                            channel
                        ] / 128.0F;
                        break;

                    case SIGNED_16BIT:
                        amplitude += ((short *) buffer)[
                            (buffer_offset >> 1) +
                            sample * session->parameters->channels +
                            channel
                        ] / 32767.0F;
                        break;

                    default:
                        fputs("Unknown sample format.\n", stderr);
                        return;
                }
            }

            wnd_cur_in[sample].i = 0.0F;
            wnd_cur_in[sample].r = amplitude;
        }

        /* Perform the FFT. */

        kiss_fft(session->cache.fft_config, wnd_cur_in, wnd_cur_out);

        /*
         * Determine the score and store it - potentially restructuring the
         * memory first.
         */

        score = _score(wnd_last, wnd_cur_out, session->sample_window_size);

        if (
            session->cache.score_buffer_limit == session->cache.score_buffer_capacity
        ) {
            if (session->cache.score_buffer_offset_index) {
                /* If possible, shrink. */

                memmove(
                    session->cache.score_buffer,
                    session->cache.score_buffer +
                        session->cache.score_buffer_offset_index,
                    sizeof(double) *
                        (session->cache.score_buffer_limit -
                            session->cache.score_buffer_offset_index)
                );

                session->cache.score_buffer_limit -=
                    session->cache.score_buffer_offset_index;

                session->cache.score_buffer_offset_index = 0;
            } else {
                unsigned long new_capacity;
                double *new_buffer;

                /* Otherwise, increase size. */

                new_capacity = session->cache.score_buffer_capacity << 1;
                new_buffer = realloc(
                    session->cache.score_buffer,
                    sizeof(double) * new_capacity
                );

                if (!new_buffer) {
                    fputs("Cannot increase score buffer size.\n", stderr);
                    return;
                }

                session->cache.score_buffer = new_buffer;
                session->cache.score_buffer_capacity = new_capacity;
            }
        }

        session->cache.score_buffer[
            session->cache.score_buffer_limit++
        ] = score;

        /* Finalization. */

        //session->cache.last_window_out = wnd_cur_out;
        //session->cache.current_window_out = wnd_last;
    }
}

void vibexec_vibeomatic_cleanup(struct vibexec_vibeomatic_session *session) {
    free(session->cache.last_window_out);
    free(session->cache.current_window_out);
    free(session->cache.current_window_in);
    free(session->cache.fft_config);
    free(session->cache.score_buffer);
}

double vibexec_vibeomatic_drop_and_score(
    struct vibexec_vibeomatic_session *session,
    struct timespec *offset
) {
    struct timespec offset_difference;

    /* Querying the past is not possible. */

    _compute_difference(
        &offset_difference,
        offset,
        &session->cache.score_buffer_offset
    );

    if (offset_difference.tv_sec < 0) {
        fputs("Score not available (past).\n", stderr);
        return 0.5;
    }

    /*
     * Increase the score buffer offset until the first valid score is the one
     * that we are looking for.
     */

    while (
        _is_ge_than(&offset_difference, session->cache.nanoseconds_per_window) &&
        session->cache.score_buffer_offset_index < session->cache.score_buffer_limit
    ) {
        session->cache.score_buffer_offset.tv_nsec +=
            (long) session->cache.nanoseconds_per_window;

        offset_difference.tv_nsec -=
            (long) session->cache.nanoseconds_per_window;

        session->cache.score_buffer_offset_index++;

        if (session->cache.score_buffer_offset.tv_nsec > 1000000000) {
            session->cache.score_buffer_offset.tv_nsec -= 1000000000;
            session->cache.score_buffer_offset.tv_sec++;
        }

        if (offset_difference.tv_nsec < 0) {
            offset_difference.tv_sec--;
            offset_difference.tv_nsec += 1000000000;
        }
    }

    /* Return the score, if possible. */

    if (session->cache.score_buffer_offset_index >= session->cache.score_buffer_limit) {
        fputs("Score not available (future).\n", stderr);
        return 0.5;
    }

    return session->cache.score_buffer[
        session->cache.score_buffer_offset_index
    ];
}

int vibexec_vibeomatic_initialize(
    struct vibexec_vibeomatic_session *session,
    const struct vibexec_schedulable_parameters *parameters,
    unsigned long sample_window_size
) {
    session->parameters = parameters;
    session->sample_window_size = sample_window_size;

    /* Cache preparation. */

    session->cache.nanoseconds_per_window =
        1000000000.0
        * ((double) sample_window_size)
        / ((double) session->parameters->sample_frequency);

    session->cache.window_size_in_bytes =
        session->sample_window_size * session->parameters->channels;

    switch (session->parameters->sample_format) {
        case SIGNED_8BIT:
            /* Nothing to do here: 8 bit = 1 byte. */
            break;

        case SIGNED_16BIT:
            session->cache.window_size_in_bytes <<= 1;
            break;

        default:
            fputs("Unknown sample format.\n", stderr);
            goto error_return;
    }

    session->cache.last_window_out = calloc(
        session->sample_window_size,
        sizeof(kiss_fft_cpx)
    );

    if (!session->cache.last_window_out) {
        fputs("Cannot allocate memory.\n", stderr);
        goto error_return;
    }

    session->cache.current_window_in = malloc(
        session->sample_window_size * sizeof(kiss_fft_cpx)
    );

    if (!session->cache.current_window_in) {
        fputs("Cannot allocate memory.\n", stderr);
        goto error_cleanup_last_window_out;
    }

    session->cache.current_window_out = malloc(
        session->sample_window_size * sizeof(kiss_fft_cpx)
    );

    if (!session->cache.current_window_out) {
        fputs("Cannot allocate memory.\n", stderr);
        goto error_cleanup_current_window_in;
    }

    session->cache.fft_config = kiss_fft_alloc(
        session->sample_window_size,
        0, NULL, NULL
    );

    if (!session->cache.fft_config) {
        fputs("Cannot allocate memory.\n", stderr);
        goto error_cleanup_current_window_out;
    }

    /* Cache preparation: score buffer */

    memset(&session->cache.score_buffer_offset, 0, sizeof(struct timespec));
    session->cache.score_buffer_limit = 0;
    session->cache.score_buffer_offset_index = 0;
    session->cache.score_buffer_capacity =
        (session->parameters->sample_frequency / session->sample_window_size)
        + 1;

    session->cache.score_buffer = malloc(
        sizeof(double) * session->cache.score_buffer_capacity
    );

    if (!session->cache.score_buffer) {
        fputs("Cannot allocate memory.\n", stderr);
        goto error_cleanup_fft_config;
    }

    /* Cache preparation: score buffer: initialize first (fixed) score */

    session->cache.score_buffer[0] = 0.0;
    session->cache.score_buffer_limit++;

    return 0;

error_cleanup_fft_config:
    free(session->cache.fft_config);
error_cleanup_current_window_out:
    free(session->cache.current_window_out);
error_cleanup_current_window_in:
    free(session->cache.current_window_in);
error_cleanup_last_window_out:
    free(session->cache.last_window_out);
error_return:
    return -1;
}

static inline int _is_ge_than(const struct timespec *left, double right) {
    return (left->tv_sec > 0) || (left->tv_nsec > right);
}

static inline double _score(
    kiss_fft_cpx *last_window,
    kiss_fft_cpx *current_window,
    unsigned long window_size
) {
    unsigned long i;
    unsigned int large_diffs;

    large_diffs = 0;

    for (i = 0; i < window_size; i++) {
        double diff = fabs(
            fabs(current_window[i].r) - fabs(last_window[i].r)
        );

        if (diff > 10.0) {
            large_diffs++;
        }
    }

    if (large_diffs > 150) large_diffs = 150;

    return large_diffs / 150.0;
}
