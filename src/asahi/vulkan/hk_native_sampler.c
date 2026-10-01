/* SPDX-License-Identifier: MIT */
#include "hk_native_sampler.h"
#include "asahi/lib/agx_native_sampler.h"
#include <string.h>

bool hk_sampler_reduction_supported(enum agx_sampler_generation g,
                                    enum hk_sampler_reduction_mode r)
{
   return agx_sampler_reduction_supported(g, (enum agx_native_sampler_reduction_mode)r);
}

bool hk_pack_native_sampler(enum agx_chip chip,
                            struct hk_native_sampler_descriptor *out,
                            const struct hk_native_sampler_state *state)
{
   static_assert(sizeof(*out) == sizeof(struct agx_native_sampler_descriptor), "sampler ABI");
   static_assert(sizeof(*state) == sizeof(struct agx_native_sampler_state), "sampler state ABI");
   struct agx_native_sampler_descriptor packed;
   struct agx_native_sampler_state semantic;
   if (!out) return false;
   memset(out, 0, sizeof(*out));
   if (!state) return false;
   memcpy(&semantic, state, sizeof(semantic));
   const bool valid = agx_pack_native_sampler(chip, &packed, &semantic);
   memcpy(out, &packed, sizeof(*out));
   return valid;
}

bool hk_pack_native_txf_sampler(enum agx_chip chip,
                                struct hk_native_sampler_descriptor *out)
{
   if (!out) return false;
   struct agx_native_sampler_descriptor packed;
   const bool valid = agx_pack_native_txf_sampler(chip, &packed);
   memcpy(out, &packed, sizeof(*out));
   return valid;
}
