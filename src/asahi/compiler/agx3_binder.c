/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_binder.h"

#include <string.h>

bool
agx3_bind_compose(enum agx_isa_encoding encoding, const char *mnemonic, int ep,
                  const struct agx3_field_kv *baseline, unsigned nbaseline,
                  const struct agx3_field_kv *overrides, unsigned noverrides,
                  uint8_t *out, uint8_t *len)
{
   if (out == NULL || len == NULL || mnemonic == NULL)
      return false;

   const agx3_table *t;
   if (encoding == AGX_ISA_ENCODING_AGX3B)
      t = &agx3b_table;
   else if (encoding == AGX_ISA_ENCODING_AGX3C)
      t = &agx3c_table;
   else
      return false;

   const int32_t form = agx3_form_index(t, mnemonic, (uint8_t)ep);
   if (form < 0)
      return false;

   uint64_t fields[AGX3_MAX_FIELDS] = {0};
   for (unsigned i = 0; i < nbaseline; ++i) {
      if (baseline[i].field >= AGX3_MAX_FIELDS)
         return false;
      fields[baseline[i].field] = baseline[i].value;
   }
   for (unsigned i = 0; i < noverrides; ++i) {
      if (overrides[i].field >= AGX3_MAX_FIELDS)
         return false;
      fields[overrides[i].field] = overrides[i].value;
   }

   uint8_t buf[AGX3_MAX_WORDS * 2];
   if (!agx3_compose(t, (uint32_t)form, fields, buf))
      return false;

   const uint8_t size = t->forms[form].size;
   memcpy(out, buf, size);
   *len = size;
   return true;
}
