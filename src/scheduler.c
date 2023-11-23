#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "player.h"
#include "scheduler.h"
#include "vibeomatic.h"

static struct {
    /* General.*/

    int initialized;

    /* Vibe-specific information. */

    struct vibexec_schedulable_parameters parameters;
    FILE *source;
    struct vibexec_vibeomatic_session session;
    struct timespec start;
    int started;

    struct {
        unsigned long buffer_size;
        void *buffer;
    } cache;
} _vibe;

void vibexec_scheduler_cleanup(void) {
    if (!_vibe.initialized) {
        fputs("Vibe not initialized.\n", stderr);
        return;
    }

    free(_vibe.cache.buffer);
    vibexec_vibeomatic_cleanup(&_vibe.session);
    fclose(_vibe.source);
    _vibe.initialized = 0;
}

int vibexec_scheduler_initialize(
    const struct vibexec_schedulable_vibe *vibe
) {
    int failure;

    if (_vibe.initialized) {
        fputs("Vibe already initialized.\n", stderr);
        goto error_return;
    }

    /* Open source file. */

    _vibe.source = fopen(vibe->path, "r");

    if (!_vibe.source) {
        fputs("Vibe not existing.\n", stderr);
        goto error_return;
    }

    /* Copy decoding settings. */

    memcpy(
        &_vibe.parameters,
        &vibe->parameters,
        sizeof(struct vibexec_schedulable_parameters)
    );

    /* Create vibe-o-matic session. */

    failure = vibexec_vibeomatic_initialize(
        &_vibe.session,
        &_vibe.parameters,
        _vibe.parameters.sample_frequency >> 6
    );

    if (failure) {
        fputs("Cannot initialize vibe-o-matic.\n", stderr);
        goto error_cleanup_source;
    }

    /* Fill cache. */

    _vibe.cache.buffer_size =
        vibe->parameters.sample_frequency * vibe->parameters.channels;

    switch (_vibe.parameters.sample_format) {
        case SIGNED_8BIT:
            /* Nothing to do here: 8 bit = 1 byte. */
            break;

        case SIGNED_16BIT:
            _vibe.cache.buffer_size <<= 1;
            break;

        default:
            fputs("Unknown vibe format.\n", stderr);
            goto error_cleanup_vibeomatic;
    }

    _vibe.cache.buffer = malloc(_vibe.cache.buffer_size);

    if (!_vibe.cache.buffer) {
        fputs("Buffer allocation failed.\n", stderr);
        goto error_cleanup_source;
    }

    /* Finalize. */

    _vibe.initialized = 1;
    return 0;

error_cleanup_vibeomatic:
    vibexec_vibeomatic_cleanup(&_vibe.session);
error_cleanup_source:
    fclose(_vibe.source);
error_return:
    return -1;
}

int vibexec_scheduler_next_buffer(struct vibexec_scheduled_buffer *buffer) {
    unsigned long actual_buffer_size;

    if (!_vibe.initialized) {
        fputs("Vibe not initialized.\n", stderr);
        return -1;
    }

    if (!_vibe.started) {
        clock_gettime(CLOCK_MONOTONIC, &_vibe.start);
        _vibe.started = 1;
    }

    /*
     * Fill the internal buffer, if possible.
     *
     * NOTE:    Cast size_t -> unsigned long is safe, because returned value is
     *          never greater than _vibe.cache.buffer_size, which is unsigned
     *          long.
     */

    actual_buffer_size = (unsigned long) fread(
        _vibe.cache.buffer,
        1, _vibe.cache.buffer_size,
        _vibe.source
    );

    if (!actual_buffer_size) {
        /* EOF */
        return -1;
    }

    /* Feed the vibe-o-matic. */

    vibexec_vibeomatic_analyze(
        &_vibe.session,
        _vibe.cache.buffer,
        actual_buffer_size
    );

    /* Pass the internal buffer to caller. */

    buffer->parameters = &_vibe.parameters;
    buffer->buffer_size = actual_buffer_size;
    buffer->buffer = _vibe.cache.buffer;

    return 0;
}

void vibexec_scheduler_yield_to_vibe(void) {
    struct timespec current_time, diff_since_start;
    double score;

    vibexec_player_update();

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    _compute_difference(&diff_since_start, &current_time, &_vibe.start);

    score = vibexec_vibeomatic_drop_and_score(
        &_vibe.session,
        &diff_since_start
    );

    struct timespec pause;

    pause.tv_nsec = (long) ((1.0 - score) * 10000000);
    pause.tv_sec = 0;

    nanosleep(&pause, NULL);
}
