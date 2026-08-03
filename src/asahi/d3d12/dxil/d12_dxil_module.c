/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * DXIL module decoder: turns the bitstream record tree into type,
 * constant, function and metadata tables — everything the NIR translator
 * (G4c) needs, with no SPIR-V and no external compiler in the path.
 *
 * Record numbering follows LLVM 3.7 (what DXIL froze on), cross-checked
 * against Mesa's own MIT-licensed DXIL writer in src/microsoft/compiler.
 */

#include "d12_dxil.h"

#include <stdlib.h>
#include <string.h>

#define MESA_LOG_TAG "d3d12agx"
#include "util/log.h"
#include "util/macros.h"

struct parse_ctx {
   struct d12_module *mod;

   /* Type table state */
   uint32_t pending_struct_name; /* index into strings, ~0 if none */

   /* Constant block state: CST_SETTYPE latches the type for what follows */
   uint32_t const_type;

   /* Function parsing state */
   struct d12_function *cur_func;
   unsigned next_func_to_define; /* module order of the next FUNCTION block */

   /* Metadata naming: MD_NAME is followed by MD_NAMED_NODE */
   const char *pending_md_name;
};

static void
fail(struct d12_module *mod, const char *msg)
{
   if (!mod->error)
      mod->error = msg;
}

/* Copy a record's char-array operands into the module string arena. */
static const char *
intern_chars(struct d12_module *mod, const uint64_t *ops, unsigned n)
{
   if (mod->strings_used + n + 1 > mod->strings_size) {
      size_t want = MAX2(mod->strings_size * 2, mod->strings_used + n + 64);
      char *grown = realloc(mod->strings, want);
      if (!grown) {
         fail(mod, "out of memory interning a string");
         return NULL;
      }
      mod->strings = grown;
      mod->strings_size = want;
   }

   char *out = mod->strings + mod->strings_used;
   for (unsigned i = 0; i < n; i++)
      out[i] = (char)ops[i];
   out[n] = '\0';
   mod->strings_used += n + 1;
   return out;
}

static struct d12_value *
push_value(struct d12_module *mod, enum d12_value_kind kind, uint32_t type)
{
   if (mod->num_values >= D12_MAX_VALUES) {
      fail(mod, "too many values in module");
      return NULL;
   }

   struct d12_value *v = &mod->values[mod->num_values++];
   memset(v, 0, sizeof(*v));
   v->kind = kind;
   v->type = type;
   return v;
}

/*
 * Types
 */
static bool
parse_type_record(struct parse_ctx *ctx, const struct d12_record *rec)
{
   struct d12_module *mod = ctx->mod;

   if (rec->code == D12_TYPE_NUMENTRY)
      return true; /* capacity hint only */

   if (rec->code == D12_TYPE_STRUCT_NAME) {
      const char *name = intern_chars(mod, rec->ops, rec->num_ops);
      ctx->pending_struct_name = 0;
      /* Stash for the STRUCT_NAMED that follows */
      if (mod->num_types < D12_MAX_TYPES)
         mod->types[mod->num_types].name = name;
      return true;
   }

   if (mod->num_types >= D12_MAX_TYPES) {
      fail(mod, "too many types");
      return false;
   }

   struct d12_type *t = &mod->types[mod->num_types];
   const char *carried_name = t->name; /* set by a preceding STRUCT_NAME */
   memset(t, 0, sizeof(*t));
   t->kind = rec->code;

   switch (rec->code) {
   case D12_TYPE_VOID:
   case D12_TYPE_FLOAT:
   case D12_TYPE_DOUBLE:
   case D12_TYPE_HALF:
   case D12_TYPE_LABEL:
   case D12_TYPE_METADATA:
   case D12_TYPE_OPAQUE:
      break;

   case D12_TYPE_INTEGER:
      if (rec->num_ops < 1)
         return true;
      t->int_bits = (unsigned)rec->ops[0];
      break;

   case D12_TYPE_POINTER:
      if (rec->num_ops < 1)
         return true;
      t->ptr.pointee = (uint32_t)rec->ops[0];
      t->ptr.addr_space = rec->num_ops > 1 ? (uint32_t)rec->ops[1] : 0;
      break;

   case D12_TYPE_ARRAY:
   case D12_TYPE_VECTOR:
      if (rec->num_ops < 2)
         return true;
      t->seq.count = rec->ops[0];
      t->seq.elem = (uint32_t)rec->ops[1];
      break;

   case D12_TYPE_FUNCTION:
      /* [vararg, retty, paramty...] */
      if (rec->num_ops < 2)
         return true;
      t->func.ret = (uint32_t)rec->ops[1];
      for (unsigned i = 2; i < rec->num_ops && t->func.num_params < 16; i++)
         t->func.params[t->func.num_params++] = (uint32_t)rec->ops[i];
      break;

   case D12_TYPE_STRUCT_ANON:
   case D12_TYPE_STRUCT_NAMED:
      /* [ispacked, eltty...] */
      for (unsigned i = 1; i < rec->num_ops && t->strct.num_members < 16; i++)
         t->strct.members[t->strct.num_members++] = (uint32_t)rec->ops[i];
      if (rec->code == D12_TYPE_STRUCT_NAMED)
         t->name = carried_name;
      break;

   default:
      /* Unknown type kinds still occupy a slot so indices stay aligned. */
      break;
   }

   mod->num_types++;
   return true;
}

/*
 * Constants. LLVM signs integers with a rotate: bit 0 is the sign.
 */
static uint64_t
decode_signed_vbr(uint64_t v)
{
   if (v & 1)
      return v == 1 ? (uint64_t)INT64_MIN : (uint64_t)(-(int64_t)(v >> 1));
   return v >> 1;
}

static bool
parse_const_record(struct parse_ctx *ctx, const struct d12_record *rec)
{
   struct d12_module *mod = ctx->mod;

   if (rec->code == D12_CST_SETTYPE) {
      if (rec->num_ops >= 1)
         ctx->const_type = (uint32_t)rec->ops[0];
      return true;
   }

   struct d12_value *v = push_value(mod, D12_VALUE_CONSTANT, ctx->const_type);
   if (!v)
      return false;

   switch (rec->code) {
   case D12_CST_NULL:
      v->is_null = true;
      break;
   case D12_CST_UNDEF:
      v->is_undef = true;
      break;
   case D12_CST_INTEGER:
      if (rec->num_ops >= 1)
         v->const_int = decode_signed_vbr(rec->ops[0]);
      break;
   case D12_CST_FLOAT:
      if (rec->num_ops >= 1)
         v->const_float = rec->ops[0];
      break;
   default:
      /* Aggregates, strings, const-exprs: slot reserved, contents are
       * G4c work (only needed once shaders reference them).
       */
      break;
   }

   return true;
}

/*
 * Metadata
 */
static bool
parse_metadata_record(struct parse_ctx *ctx, const struct d12_record *rec)
{
   struct d12_module *mod = ctx->mod;

   if (rec->code == D12_MD_NAME) {
      ctx->pending_md_name = intern_chars(mod, rec->ops, rec->num_ops);
      return true;
   }

   if (rec->code == D12_MD_NAMED_NODE) {
      if (mod->num_named_md >= D12_MAX_NAMED_MD)
         return true;

      struct d12_named_md *nmd = &mod->named_md[mod->num_named_md++];
      memset(nmd, 0, sizeof(*nmd));
      nmd->name = ctx->pending_md_name;
      for (unsigned i = 0; i < rec->num_ops && nmd->num_nodes < 16; i++)
         nmd->nodes[nmd->num_nodes++] = rec->ops[i];
      ctx->pending_md_name = NULL;
      return true;
   }

   if (mod->num_metadata >= D12_MAX_METADATA) {
      fail(mod, "too many metadata nodes");
      return false;
   }

   struct d12_metadata *md = &mod->metadata[mod->num_metadata++];
   memset(md, 0, sizeof(*md));
   md->kind = rec->code;

   switch (rec->code) {
   case D12_MD_STRING:
      md->string = intern_chars(mod, rec->ops, rec->num_ops);
      break;

   case D12_MD_VALUE:
      /* [type, value] */
      if (rec->num_ops >= 2) {
         md->value_type = (uint32_t)rec->ops[0];
         md->value_id = rec->ops[1];
      }
      break;

   case D12_MD_NODE:
      /* Operands are metadata ids biased by 1; 0 means null. */
      for (unsigned i = 0; i < rec->num_ops && md->node.num_ops < 64; i++) {
         unsigned slot = md->node.num_ops++;
         if (rec->ops[i] == 0) {
            md->node.op_is_null[slot] = true;
            md->node.ops[slot] = 0;
         } else {
            md->node.ops[slot] = rec->ops[i] - 1;
         }
      }
      break;

   default:
      break;
   }

   return true;
}

/*
 * Module-level records
 */
static bool
parse_module_record(struct parse_ctx *ctx, const struct d12_record *rec)
{
   struct d12_module *mod = ctx->mod;

   switch (rec->code) {
   case D12_MODULE_GLOBALVAR: {
      /* [pointer type, isconst, initid, ...] */
      struct d12_value *v = push_value(
         mod, D12_VALUE_GLOBAL, rec->num_ops >= 1 ? (uint32_t)rec->ops[0] : 0);
      if (!v)
         return false;
      break;
   }

   case D12_MODULE_FUNCTION: {
      /* [type, callingconv, isproto, ...] */
      if (mod->num_functions >= D12_MAX_FUNCTIONS) {
         fail(mod, "too many functions");
         return false;
      }

      struct d12_function *f = &mod->functions[mod->num_functions];
      memset(f, 0, sizeof(*f));
      f->type = rec->num_ops >= 1 ? (uint32_t)rec->ops[0] : 0;
      f->is_declaration = rec->num_ops >= 3 && rec->ops[2] != 0;
      f->value_index = mod->num_values;

      struct d12_value *v =
         push_value(mod, D12_VALUE_FUNCTION, f->type);
      if (!v)
         return false;
      v->func_index = mod->num_functions;

      mod->num_functions++;
      break;
   }

   default:
      break;
   }

   return true;
}

/*
 * Function bodies. LLVM 3.2+ encodes value operands as RELATIVE ids:
 * (current value number - operand). Resolve them here so downstream code
 * sees absolute indices.
 */
static uint32_t
rel_to_abs(struct parse_ctx *ctx, uint64_t rel)
{
   uint32_t cur = ctx->mod->num_values;
   if (rel == 0 || rel > cur)
      return ~0u; /* forward reference or invalid; G4c resolves phis */
   return cur - (uint32_t)rel;
}

static bool
parse_function_record(struct parse_ctx *ctx, const struct d12_record *rec)
{
   struct d12_module *mod = ctx->mod;
   struct d12_function *f = ctx->cur_func;

   if (!f)
      return true;

   if (rec->code == D12_FUNC_DECLAREBLOCKS) {
      f->num_blocks = rec->num_ops >= 1 ? (unsigned)rec->ops[0] : 0;
      return true;
   }

   /* Debug location records carry no dataflow. */
   if (rec->code == D12_FUNC_DEBUG_LOC || rec->code == D12_FUNC_DEBUG_LOC_AGAIN)
      return true;

   if (f->num_insts >= D12_MAX_INSTS) {
      fail(mod, "too many instructions in function");
      return false;
   }

   struct d12_inst *inst = &f->insts[f->num_insts];
   memset(inst, 0, sizeof(*inst));
   inst->code = rec->code;
   inst->result_type = ~0u;
   inst->result_value = ~0u;

   bool defines_value = false;

   switch (rec->code) {
   case D12_FUNC_INST_CALL: {
      /* [paramattr, cc, fnty?, fnid, args...]. Bit 15 of the cc field
       * (CALL_EXPLICIT_TYPE) says whether the function type is spelled
       * out before the callee. Everything from the callee on is a
       * relative value id.
       */
      if (rec->num_ops < 3) {
         fail(mod, "malformed call record");
         return false;
      }

      unsigned opnum = 2;
      if ((rec->ops[1] >> 15) & 1) {
         inst->extra[inst->num_extra++] = rec->ops[opnum]; /* fnty */
         opnum++;
      }

      /* ops[0] is the callee; ops[1..] are the arguments. */
      for (; opnum < rec->num_ops && inst->num_ops < D12_MAX_INST_OPS; opnum++)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[opnum]);

      /* A call defines a value only if the callee does not return void.
       * Getting this wrong shifts every later value index in the
       * function, so resolve it from the callee's type here.
       */
      defines_value = false;
      if (inst->num_ops >= 1 && inst->ops[0] < mod->num_values) {
         uint32_t fty = mod->values[inst->ops[0]].type;

         /* Function values are typed as pointer-to-function in LLVM. */
         if (fty < mod->num_types &&
             mod->types[fty].kind == D12_TYPE_POINTER)
            fty = mod->types[fty].ptr.pointee;

         if (fty < mod->num_types &&
             mod->types[fty].kind == D12_TYPE_FUNCTION) {
            uint32_t ret = mod->types[fty].func.ret;
            bool returns_void = ret < mod->num_types &&
                                mod->types[ret].kind == D12_TYPE_VOID;
            if (!returns_void) {
               defines_value = true;
               inst->result_type = ret;
            }
         }
      }
      break;
   }

   case D12_FUNC_INST_BINOP:
      /* [lhs, rhs, opcode, (flags)] */
      if (rec->num_ops >= 3) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[1]);
         inst->extra[inst->num_extra++] = rec->ops[2];
      }
      defines_value = true;
      break;

   case D12_FUNC_INST_CAST:
      /* [value, desttype, castopcode] */
      if (rec->num_ops >= 3) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         inst->result_type = (uint32_t)rec->ops[1];
         inst->extra[inst->num_extra++] = rec->ops[2];
      }
      defines_value = true;
      break;

   case D12_FUNC_INST_CMP:
   case D12_FUNC_INST_CMP2:
      if (rec->num_ops >= 3) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[1]);
         inst->extra[inst->num_extra++] = rec->ops[2];
      }
      defines_value = true;
      break;

   case D12_FUNC_INST_SELECT:
   case D12_FUNC_INST_VSELECT:
      for (unsigned i = 0; i < rec->num_ops && inst->num_ops < 3; i++)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[i]);
      defines_value = true;
      break;

   case D12_FUNC_INST_EXTRACTVAL:
      if (rec->num_ops >= 2) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         for (unsigned i = 1; i < rec->num_ops && inst->num_extra < 4; i++)
            inst->extra[inst->num_extra++] = rec->ops[i];
      }
      defines_value = true;
      break;

   case D12_FUNC_INST_RET:
      if (rec->num_ops >= 1)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
      break;

   case D12_FUNC_INST_BR:
      /* [bb#] or [true bb#, false bb#, cond] */
      for (unsigned i = 0; i < rec->num_ops && inst->num_extra < 4; i++)
         inst->extra[inst->num_extra++] = rec->ops[i];
      if (rec->num_ops >= 3)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[2]);
      break;

   case D12_FUNC_INST_STORE:
      /* [ptr, val, align, isvolatile] */
      if (rec->num_ops >= 2) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[1]);
      }
      break;

   case D12_FUNC_INST_LOAD:
      /* [ptr, ty, align, isvolatile] */
      if (rec->num_ops >= 2) {
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[0]);
         inst->result_type = (uint32_t)rec->ops[1];
      }
      defines_value = true;
      break;

   case D12_FUNC_INST_GEP:
   case D12_FUNC_INST_GEP_OLD:
      for (unsigned i = (rec->code == D12_FUNC_INST_GEP ? 2 : 0);
           i < rec->num_ops && inst->num_ops < D12_MAX_INST_OPS; i++)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[i]);
      defines_value = true;
      break;

   case D12_FUNC_INST_PHI:
      /* [ty, (val, bb)...] — values are signed-relative here */
      if (rec->num_ops >= 1)
         inst->result_type = (uint32_t)rec->ops[0];
      defines_value = true;
      break;

   case D12_FUNC_INST_ATOMICRMW:
   case D12_FUNC_INST_CMPXCHG:
      for (unsigned i = 0; i < rec->num_ops && inst->num_ops < 3; i++)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[i]);
      defines_value = true;
      break;

   case D12_FUNC_INST_EXTRACTELT:
   case D12_FUNC_INST_INSERTELT:
   case D12_FUNC_INST_SHUFFLEVEC:
      for (unsigned i = 0; i < rec->num_ops && inst->num_ops < 3; i++)
         inst->ops[inst->num_ops++] = rel_to_abs(ctx, rec->ops[i]);
      defines_value = true;
      break;

   case D12_FUNC_INST_ALLOCA:
      if (rec->num_ops >= 1)
         inst->result_type = (uint32_t)rec->ops[0];
      defines_value = true;
      break;

   default:
      /* Unhandled instruction: recorded so the dump can report it, but it
       * must not silently define a value and shift every later index.
       */
      break;
   }

   /* A call to a void function defines nothing. We cannot know that until
    * the callee is resolved, so CALL provisionally defines a value and G4c
    * corrects using the callee type. For everything else the rule above is
    * exact.
    */
   if (defines_value) {
      struct d12_value *v =
         push_value(mod, D12_VALUE_INSTRUCTION, inst->result_type);
      if (!v)
         return false;
      inst->result_value = mod->num_values - 1;
   }

   f->num_insts++;
   return true;
}

/*
 * Visitor plumbing
 */
static bool
on_enter_block(void *user, unsigned block_id, unsigned depth)
{
   struct parse_ctx *ctx = user;
   struct d12_module *mod = ctx->mod;

   if (block_id == D12_BLOCK_CONSTANTS)
      ctx->const_type = 0;

   if (block_id == D12_BLOCK_FUNCTION) {
      /* Bodies appear in module order, skipping declarations. */
      while (ctx->next_func_to_define < mod->num_functions &&
             mod->functions[ctx->next_func_to_define].is_declaration)
         ctx->next_func_to_define++;

      if (ctx->next_func_to_define >= mod->num_functions) {
         fail(mod, "function body without a matching declaration");
         return false;
      }

      struct d12_function *f = &mod->functions[ctx->next_func_to_define++];
      f->insts = calloc(D12_MAX_INSTS, sizeof(*f->insts));
      if (!f->insts) {
         fail(mod, "out of memory for function body");
         return false;
      }

      /* Arguments occupy value slots before any instruction. */
      f->first_value = mod->num_values;
      if (f->type < mod->num_types) {
         const struct d12_type *ft = &mod->types[f->type];
         if (ft->kind == D12_TYPE_FUNCTION) {
            for (unsigned i = 0; i < ft->func.num_params; i++) {
               if (!push_value(mod, D12_VALUE_ARGUMENT, ft->func.params[i]))
                  return false;
            }
         }
      }

      ctx->cur_func = f;
   }

   return true;
}

static bool
on_exit_block(void *user, unsigned block_id, unsigned depth)
{
   struct parse_ctx *ctx = user;

   if (block_id == D12_BLOCK_FUNCTION)
      ctx->cur_func = NULL;

   return true;
}

static bool
on_record(void *user, unsigned block_id, unsigned depth,
          const struct d12_record *rec, unsigned abbrev_id)
{
   struct parse_ctx *ctx = user;

   switch (block_id) {
   case D12_BLOCK_MODULE:
      return parse_module_record(ctx, rec);
   case D12_BLOCK_TYPE:
      return parse_type_record(ctx, rec);
   case D12_BLOCK_CONSTANTS:
      return parse_const_record(ctx, rec);
   case D12_BLOCK_METADATA:
      return parse_metadata_record(ctx, rec);
   case D12_BLOCK_FUNCTION:
      return parse_function_record(ctx, rec);
   case D12_BLOCK_VALUE_SYMTAB:
      /* [valueid, chars...] — names for globals, functions, and locals */
      if (rec->code == D12_VST_ENTRY && rec->num_ops >= 2) {
         uint64_t id = rec->ops[0];
         const char *name =
            intern_chars(ctx->mod, rec->ops + 1, rec->num_ops - 1);
         if (id < ctx->mod->num_values)
            ctx->mod->values[id].name = name;
      }
      return true;
   default:
      return true;
   }
}

bool
d12_dxil_module_parse(struct d12_module *mod, const uint8_t *bitcode,
                      size_t size_B)
{
   memset(mod, 0, sizeof(*mod));

   struct d12_bitstream bs;
   d12_bitstream_init(&bs, bitcode, size_B);

   struct parse_ctx ctx = {
      .mod = mod,
      .pending_struct_name = ~0u,
      .next_func_to_define = 0,
   };

   struct d12_block_visitor visitor = {
      .user = &ctx,
      .enter_block = on_enter_block,
      .record = on_record,
      .exit_block = on_exit_block,
   };

   if (!d12_dxil_walk(&bs, &visitor)) {
      if (!mod->error)
         mod->error = bs.error_msg ? bs.error_msg : "bitstream walk failed";
      return false;
   }

   /* Names arrive in VALUE_SYMTAB after the function table is built; copy
    * them onto the functions for convenience.
    */
   for (unsigned i = 0; i < mod->num_functions; i++) {
      struct d12_function *f = &mod->functions[i];
      if (f->value_index < mod->num_values)
         f->name = mod->values[f->value_index].name;
   }

   return mod->error == NULL;
}

void
d12_dxil_module_finish(struct d12_module *mod)
{
   for (unsigned i = 0; i < mod->num_functions; i++)
      free(mod->functions[i].insts);
   free(mod->strings);
   memset(mod, 0, sizeof(*mod));
}

const struct d12_named_md *
d12_dxil_find_named_md(const struct d12_module *mod, const char *name)
{
   for (unsigned i = 0; i < mod->num_named_md; i++) {
      if (mod->named_md[i].name && !strcmp(mod->named_md[i].name, name))
         return &mod->named_md[i];
   }
   return NULL;
}
