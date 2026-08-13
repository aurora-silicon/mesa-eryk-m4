/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_device_removal_core.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                     \
   do {                                                                      \
      if (!(condition)) {                                                    \
         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                 #condition);                                                \
         exit(1);                                                            \
      }                                                                      \
   } while (0)

struct callback_record {
   unsigned calls;
   int32_t reason;
};

static void
record_removal(void *userdata, int32_t reason)
{
   struct callback_record *record = userdata;
   ++record->calls;
   record->reason = reason;
}

struct race_argument {
   struct d12_device_removal_state *state;
   int32_t reason;
};

static void *
race_report(void *userdata)
{
   struct race_argument *argument = userdata;
   (void)d12_device_removal_report(argument->state, argument->reason);
   return NULL;
}

int
main(void)
{
   struct d12_device_removal_state state;
   struct callback_record record = {0};

   d12_device_removal_state_init(&state);
   CHECK(d12_device_removal_reason(&state) == 0);
   CHECK(!d12_device_removal_report(NULL, -1));
   CHECK(!d12_device_removal_report(&state, 0));
   CHECK(!d12_device_removal_set_callback(NULL, record_removal, &record));
   CHECK(!d12_device_removal_set_callback(&state, NULL, &record));
   CHECK(d12_device_removal_set_callback(&state, record_removal, &record));
   CHECK(!d12_device_removal_set_callback(&state, record_removal, &record));

   CHECK(d12_device_removal_report(&state, (int32_t)0x887a0005u));
   CHECK(record.calls == 1);
   CHECK(record.reason == (int32_t)0x887a0005u);
   CHECK(d12_device_removal_reason(&state) == (int32_t)0x887a0005u);

   /* A later failure neither changes the retained cause nor calls the
    * frontend again. */
   CHECK(!d12_device_removal_report(&state, (int32_t)0x887a0006u));
   CHECK(record.calls == 1);
   CHECK(d12_device_removal_reason(&state) == (int32_t)0x887a0005u);

   /* Late registration receives the retained transition exactly once. */
   d12_device_removal_state_init(&state);
   record = (struct callback_record){0};
   CHECK(d12_device_removal_report(&state, -7));
   CHECK(record.calls == 0);
   CHECK(d12_device_removal_set_callback(&state, record_removal, &record));
   CHECK(record.calls == 1 && record.reason == -7);
   CHECK(!d12_device_removal_report(&state, -8));
   CHECK(record.calls == 1 && d12_device_removal_reason(&state) == -7);

   /* Concurrent submit/retirement failures still publish one transition and
    * invoke the runtime once. Which valid failure wins is intentionally not
    * ordered. */
   enum { thread_count = 16 };
   pthread_t threads[thread_count];
   struct race_argument arguments[thread_count];
   d12_device_removal_state_init(&state);
   record = (struct callback_record){0};
   CHECK(d12_device_removal_set_callback(&state, record_removal, &record));
   for (int32_t i = 0; i < thread_count; ++i) {
      arguments[i] = (struct race_argument){
         .state = &state,
         .reason = -(i + 1),
      };
      CHECK(pthread_create(&threads[i], NULL, race_report, &arguments[i]) ==
            0);
   }
   for (unsigned i = 0; i < thread_count; ++i)
      CHECK(pthread_join(threads[i], NULL) == 0);
   CHECK(record.calls == 1);
   CHECK(record.reason <= -1 && record.reason >= -thread_count);
   CHECK(d12_device_removal_reason(&state) == record.reason);

   puts("d12_device_removal_core_tests: PASS");
   return 0;
}
