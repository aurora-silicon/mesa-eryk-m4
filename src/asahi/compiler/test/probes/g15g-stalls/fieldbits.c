/* SPDX-License-Identifier: MIT */
/* For a form + field vector, report which encoded bits each field toggles. usage: fieldbits FORM f:v ... -- FIELD... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agx3_compose.h"
int main(int argc, char **argv) {
   uint32_t form = atoi(argv[1]);
   uint64_t f[AGX3_MAX_FIELDS] = {0};
   int i = 2;
   for (; i < argc && strcmp(argv[i], "--"); i++) { unsigned k; unsigned long long v; sscanf(argv[i], "%u:%llu", &k, &v); f[k] = v; }
   uint8_t base[32], out[32]; unsigned sz = agx3b_table.forms[form].size;
   if (!agx3_compose(&agx3b_table, form, f, base)) { printf("base fails\n"); return 1; }
   for (i++; i < argc; i++) {
      unsigned k = atoi(argv[i]); uint64_t save = f[k]; f[k] = save ^ 1;
      if (!agx3_compose(&agx3b_table, form, f, out)) { printf("f%u: reject\n", k); f[k] = save; continue; }
      printf("f%u:", k);
      for (unsigned b = 0; b < sz * 8; b++) if ((out[b/8] ^ base[b/8]) & (1 << (b%8))) printf(" %u", b);
      printf("\n"); f[k] = save;
   }
}
