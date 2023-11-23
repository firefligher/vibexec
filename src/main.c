#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "player.h"
#include "scheduler.h"

int main(int argc, char *argv[]) {
    struct vibexec_schedulable_vibe vibe;
    pid_t child_pid;
    int child_status;

    if (argc < 2) {
        fputs("No program provided.\n", stderr);
        return 1;
    }

    vibe.parameters.channels = 2;
    vibe.parameters.sample_format = SIGNED_16BIT;
    vibe.parameters.sample_frequency = 48000;
    vibe.path = "sample.pcm";

    vibexec_player_initialize();
    vibexec_scheduler_initialize(&vibe);

    if ((child_pid = fork()) == -1) {
        fputs("Fork failed.\n", stderr);
        return 1;
    }

    if (child_pid == 0) {
        int status;

        /* Ask the parent to trace me. */

        status = ptrace(PTRACE_TRACEME, 0, 0, 0);

        if (status == -1) {
            fputs("Trace request failed.\n", stderr);
            return 1;
        }

        status = raise(SIGSTOP);

        if (status) {
            fputs("Waiting for parent failed.\n", stderr);
            return 1;
        }

        /* Launch the actual application. */

        status = execvp(argv[1], &argv[1]);

        /*
         * The execvp call does not return, if successful. Branching only for
         * esthetic reasons.
         */

        if (status) {
            fprintf(stderr, "Failed launching '%s'.\n", argv[1]);
            return 1;
        }

        return 0;
    }

    /* Wait for child. */

    waitpid(child_pid, NULL, 0);

    /* Loop until termination. */

    do {
        vibexec_scheduler_yield_to_vibe();
        ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL);
        waitpid(child_pid, &child_status, 0);
    } while (!WIFEXITED(child_status) && !WIFSIGNALED(child_status));

    return 0;
}
