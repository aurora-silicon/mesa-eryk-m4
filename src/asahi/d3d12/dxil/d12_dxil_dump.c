/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * Structural dump tool for the DXIL bitstream reader (G4a acceptance):
 * walks a .dxil/.bc file and prints the block/record tree. If this agrees
 * with the corpus on every blob — no parse errors, plausible module
 * structure — the bitstream layer is trustworthy enough to build the
 * module decoder on.
 */

#include "d12_dxil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *
block_name(unsigned id)
{
   switch (id) {
   case D12_BLOCK_BLOCKINFO: return "BLOCKINFO";
   case D12_BLOCK_MODULE: return "MODULE";
   case D12_BLOCK_PARAMATTR: return "PARAMATTR";
   case D12_BLOCK_PARAMATTR_GROUP: return "PARAMATTR_GROUP";
   case D12_BLOCK_CONSTANTS: return "CONSTANTS";
   case D12_BLOCK_FUNCTION: return "FUNCTION";
   case D12_BLOCK_VALUE_SYMTAB: return "VALUE_SYMTAB";
   case D12_BLOCK_METADATA: return "METADATA";
   case D12_BLOCK_METADATA_ATTACHMENT: return "METADATA_ATTACHMENT";
   case D12_BLOCK_TYPE: return "TYPE";
   case 13: return "IDENTIFICATION";
   case 18: return "USELIST";
   case 19: return "MODULE_STRTAB";
   case 20: return "GLOBALVAL_SUMMARY";
   case 21: return "OPERAND_BUNDLE_TAGS";
   case 22: return "METADATA_KIND";
   default: return "?";
   }
}

struct dump_state {
   unsigned records;
   unsigned blocks;
   bool verbose;
};

static bool
on_enter(void *user, unsigned block_id, unsigned depth)
{
   struct dump_state *st = user;
   st->blocks++;
   printf("%*s[block %u %s]\n", depth * 2, "", block_id,
          block_name(block_id));
   return true;
}

static bool
on_record(void *user, unsigned block_id, unsigned depth,
          const struct d12_record *rec, unsigned abbrev_id)
{
   struct dump_state *st = user;
   st->records++;

   if (!st->verbose)
      return true;

   printf("%*s  rec code=%u abbrev=%u ops=%u", depth * 2, "", rec->code,
          abbrev_id, rec->num_ops);
   for (unsigned i = 0; i < rec->num_ops && i < 8; i++)
      printf(" %llu", (unsigned long long)rec->ops[i]);
   if (rec->num_ops > 8)
      printf(" ...");
   if (rec->blob_size)
      printf(" blob=%zuB", rec->blob_size);
   printf("\n");
   return true;
}

static bool
on_exit_block(void *user, unsigned block_id, unsigned depth)
{
   return true;
}

int
main(int argc, char **argv)
{
   bool verbose = argc > 1 && !strcmp(argv[1], "-v");
   const char *path = argv[verbose ? 2 : 1];

   if (!path) {
      fprintf(stderr, "usage: %s [-v] shader.dxil\n", argv[0]);
      return 2;
   }

   FILE *f = fopen(path, "rb");
   if (!f) {
      fprintf(stderr, "cannot open %s\n", path);
      return 2;
   }

   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   fseek(f, 0, SEEK_SET);

   uint8_t *data = malloc(size);
   if (!data || fread(data, 1, size, f) != (size_t)size) {
      fprintf(stderr, "cannot read %s\n", path);
      return 2;
   }
   fclose(f);

   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   if (!d12_dxil_find_bitcode(data, size, &bc, &bc_size, &why)) {
      fprintf(stderr, "%s: %s\n", path, why);
      return 1;
   }

   printf("%s: %zu bytes of bitcode\n", path, bc_size);

   struct d12_bitstream bs;
   d12_bitstream_init(&bs, bc, bc_size);

   struct dump_state st = {.verbose = verbose};
   struct d12_block_visitor visitor = {
      .user = &st,
      .enter_block = on_enter,
      .record = on_record,
      .exit_block = on_exit_block,
   };

   if (!d12_dxil_walk(&bs, &visitor)) {
      fprintf(stderr, "PARSE FAILED: %s (at bit %llu of %llu)\n",
              bs.error_msg ? bs.error_msg : "visitor abort",
              (unsigned long long)bs.pos_bits,
              (unsigned long long)bc_size * 8);
      return 1;
   }

   printf("OK: %u blocks, %u records\n", st.blocks, st.records);
   free(data);
   return 0;
}
