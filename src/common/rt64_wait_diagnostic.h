#pragma once

#include <chrono>
#include <cstdio>
#if defined(__ANDROID__)
#include <sys/prctl.h>
#endif

namespace RT64 {
    // Optional Android diagnosis: preserve the predicate and never proceed
    // after a timeout. Only report sustained active frame waits, not idle work.
    template <typename Condition, typename Lock, typename Predicate, typename Report>
    void waitWithDiagnostic(Condition &condition, Lock &lock, Predicate predicate, Report report) {
#if defined(__ANDROID__) && defined(DORA64_ANDROID_DIAGNOSTICS)
        unsigned timeouts = 0;
        while (!condition.wait_for(lock, std::chrono::seconds(5), predicate)) {
            if ((timeouts++ % 6) == 0) {
                char name[16] = {};
                prctl(PR_GET_NAME, name);
                fprintf(stderr, "Dora64 wait timeout: thread=%s ", name);
                report();
                fflush(stderr);
            }
        }
        if (timeouts) {
            fprintf(stderr, "Dora64 timed wait resumed after at least %u seconds.\n", timeouts * 5);
        }
#else
        condition.wait(lock, predicate);
#endif
    }
}
