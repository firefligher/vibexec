#ifndef _VIBEXEC_SCHEDULER_H_
#define _VIBEXEC_SCHEDULER_H_

struct vibexec_schedulable_parameters {
    unsigned int channels;
    unsigned long sample_frequency;

    enum {
        SIGNED_8BIT,
        SIGNED_16BIT
    } sample_format;
};

struct vibexec_schedulable_vibe {
    const char *path;
    struct vibexec_schedulable_parameters parameters;
};

struct vibexec_scheduled_buffer {
    const struct vibexec_schedulable_parameters *parameters;
    unsigned int buffer_size;
    const void *buffer;
};

void vibexec_scheduler_cleanup(void);
int vibexec_scheduler_initialize(const struct vibexec_schedulable_vibe *vibe);
int vibexec_scheduler_next_buffer(struct vibexec_scheduled_buffer *buffer);
void vibexec_scheduler_yield_to_vibe(void);

#endif
