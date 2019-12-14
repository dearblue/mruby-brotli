#include <brotli/decode.h>
#include <brotli/encode.h>
#include <mruby.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/class.h>
#include <mruby/error.h>
#include <mruby-aux.h>
#include <mruby-aux/string/growup.h>
#include <mruby-aux/string.h>
#include <mruby-aux/scanhash.h>
#include <mruby-aux/fakedin.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#ifndef SSIZE_MAX
# define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif

#define MIN(A, B)                   ((A) < (B) ? (A) : (B))

#ifdef MRB_INT16
#   define EXT_INBUF_SIZE              (1 << 10)
#   define EXT_DEFAULT_OUTPUT_SIZE     (1 << 10)
#   define EXT_DEFAULT_OUTBUF_SIZE     (1 << 10)
#   define EXT_PARTIAL_READ_SIZE       (1 << 10)
#else
#   define EXT_INBUF_SIZE              (1 << 24)
#   define EXT_DEFAULT_OUTPUT_SIZE     (256 << 10)
#   define EXT_DEFAULT_OUTBUF_SIZE     (256 << 10)
#   define EXT_PARTIAL_READ_SIZE       (1 << 20)
#endif

#define id_initialize   mrb_intern_cstr(mrb, "initialize")
#define id_op_lsh       mrb_intern_cstr(mrb, "<<")
#define id_read         mrb_intern_cstr(mrb, "read")

static int
check_range_by_size_t(mrb_int n)
{
    if (n < 0 || n > MRBX_STR_MAX) {
        return 0;
    } else {
        return 1;
    }
}

static size_t
convert_to_size_t(MRB, VALUE v)
{
    if (NIL_P(v)) { return (size_t)-1; }

    mrb_int n = mrb_int(mrb, v);

    if (!check_range_by_size_t(n)) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "wrong negative or huge number - %S (expect zero to %S)",
                   v, mrb_fixnum_value(MRBX_STR_MAX));
    }

    return (size_t)n;
}

static int
convert_to_quality(MRB, VALUE quality)
{
    if (NIL_P(quality)) {
        return BROTLI_DEFAULT_QUALITY;
    } else if (mrb_string_p(quality) || mrb_symbol_p(quality)) {
        const char *str = mrbx_get_const_cstr(mrb, quality);

        if (strcasecmp(str, "max") == 0 || strcasecmp(str, "best") == 0) {
            return BROTLI_MAX_QUALITY;
        } else if (strcasecmp(str, "min") == 0 || strcasecmp(str, "fast") == 0) {
            return BROTLI_MIN_QUALITY;
        } else if (strcasecmp(str, "default") == 0) {
            return BROTLI_DEFAULT_QUALITY;
        } else {
            mrb_raisef(mrb, E_ARGUMENT_ERROR,
                       "wrong quality value - %S (expect \"min\", \"max\", \"fast\", \"best\", \"default\", integer or nil)",
                       quality);
        }
    } else {
        return mrb_int(mrb, quality);
    }
}

static int
convert_to_lgwin(MRB, VALUE lgwin)
{
    if (NIL_P(lgwin)) {
        return BROTLI_DEFAULT_WINDOW;
    } else if (mrb_string_p(lgwin) || mrb_symbol_p(lgwin)) {
        const char *str = mrbx_get_const_cstr(mrb, lgwin);

        if (strcasecmp(str, "max") == 0) {
            return BROTLI_MAX_WINDOW_BITS;
        } else if (strcasecmp(str, "min") == 0) {
            return BROTLI_MIN_WINDOW_BITS;
        } else if (strcasecmp(str, "default") == 0) {
            return BROTLI_DEFAULT_WINDOW;
        } else {
            mrb_raisef(mrb, E_ARGUMENT_ERROR,
                       "wrong lgwin value - %S (expect \"min\", \"max\", \"default\", integer or nil)",
                       lgwin);
        }
    } else {
        return mrb_int(mrb, lgwin);
    }
}

static int
convert_to_mode(MRB, VALUE mode)
{
    if (NIL_P(mode)) {
        return BROTLI_DEFAULT_MODE;
    } else if (mrb_string_p(mode) || mrb_symbol_p(mode)) {
        const char *str = mrbx_get_const_cstr(mrb, mode);

        if (strcasecmp(str, "generic") == 0) {
            return BROTLI_MODE_GENERIC;
        } else if (strcasecmp(str, "text") == 0) {
            return BROTLI_MODE_TEXT;
        } else if (strcasecmp(str, "font") == 0) {
            return BROTLI_MODE_FONT;
        } else if (strcasecmp(str, "default") == 0) {
            return BROTLI_DEFAULT_MODE;
        } else {
            mrb_raisef(mrb, E_ARGUMENT_ERROR,
                       "wrong mode value - %S (expect \"generic\", \"text\", \"font\", \"default\", integer or nil)",
                       mode);
        }
    } else {
        return mrb_int(mrb, mode);
    }
}

static VALUE
aux_brotli_decoder_result_string(MRB, BrotliDecoderResult ok)
{
    const char *mesg;

    switch (ok) {
    case BROTLI_DECODER_RESULT_ERROR:
        mesg = "general error";
        break;
    case BROTLI_DECODER_RESULT_SUCCESS:
        mesg = "success";
        break;
    case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
        mesg = "need more input";
        break;
    case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
        mesg = "need more output";
        break;
    default:
        mesg = "unknown";
        break;
    }

    return mrb_str_new_cstr(mrb, mesg);
}

static void
aux_brotli_decoder_error(MRB, BrotliDecoderResult ok)
{
    mrb_raisef(mrb, E_RUNTIME_ERROR,
               "failed BrotliDecoderDecompressStream() - %S (0x%S)",
               aux_brotli_decoder_result_string(mrb, ok),
               VALUE(mrbx_str_new_as_hexdigest(mrb, ok, 4)));
}

/* module Brotli::Constants */

static void
init_constants(MRB, struct RClass *mBrotli)
{
    struct RClass *mConstants = mrb_define_module_under(mrb, mBrotli, "Constants");
    mrb_include_module(mrb, mBrotli, mConstants);
    mrb_define_const(mrb, mConstants, "BROTLI_MIN_WINDOW_BITS", VALUE((mrb_int)BROTLI_MIN_WINDOW_BITS));
    mrb_define_const(mrb, mConstants, "BROTLI_MAX_WINDOW_BITS", VALUE((mrb_int)BROTLI_MAX_WINDOW_BITS));
    mrb_define_const(mrb, mConstants, "BROTLI_MIN_INPUT_BLOCK_BITS", VALUE((mrb_int)BROTLI_MIN_INPUT_BLOCK_BITS));
    mrb_define_const(mrb, mConstants, "BROTLI_MAX_INPUT_BLOCK_BITS", VALUE((mrb_int)BROTLI_MAX_INPUT_BLOCK_BITS));
    mrb_define_const(mrb, mConstants, "BROTLI_MIN_QUALITY", VALUE((mrb_int)BROTLI_MIN_QUALITY));
    mrb_define_const(mrb, mConstants, "BROTLI_MAX_QUALITY", VALUE((mrb_int)BROTLI_MAX_QUALITY));
    mrb_define_const(mrb, mConstants, "BROTLI_MODE_GENERIC", VALUE((mrb_int)BROTLI_MODE_GENERIC));
    mrb_define_const(mrb, mConstants, "BROTLI_MODE_TEXT", VALUE((mrb_int)BROTLI_MODE_TEXT));
    mrb_define_const(mrb, mConstants, "BROTLI_MODE_FONT", VALUE((mrb_int)BROTLI_MODE_FONT));
    mrb_define_const(mrb, mConstants, "BROTLI_DEFAULT_QUALITY", VALUE((mrb_int)BROTLI_DEFAULT_QUALITY));
    mrb_define_const(mrb, mConstants, "BROTLI_DEFAULT_WINDOW", VALUE((mrb_int)BROTLI_DEFAULT_WINDOW));
    mrb_define_const(mrb, mConstants, "BROTLI_DEFAULT_MODE", VALUE((mrb_int)BROTLI_DEFAULT_MODE));

    mrb_define_const(mrb, mBrotli, "MIN_WINDOW_BITS", VALUE((mrb_int)BROTLI_MIN_WINDOW_BITS));
    mrb_define_const(mrb, mBrotli, "MAX_WINDOW_BITS", VALUE((mrb_int)BROTLI_MAX_WINDOW_BITS));
    mrb_define_const(mrb, mBrotli, "MIN_INPUT_BLOCK_BITS", VALUE((mrb_int)BROTLI_MIN_INPUT_BLOCK_BITS));
    mrb_define_const(mrb, mBrotli, "MAX_INPUT_BLOCK_BITS", VALUE((mrb_int)BROTLI_MAX_INPUT_BLOCK_BITS));
    mrb_define_const(mrb, mBrotli, "MIN_QUALITY", VALUE((mrb_int)BROTLI_MIN_QUALITY));
    mrb_define_const(mrb, mBrotli, "MAX_QUALITY", VALUE((mrb_int)BROTLI_MAX_QUALITY));
    mrb_define_const(mrb, mBrotli, "MODE_GENERIC", VALUE((mrb_int)BROTLI_MODE_GENERIC));
    mrb_define_const(mrb, mBrotli, "MODE_TEXT", VALUE((mrb_int)BROTLI_MODE_TEXT));
    mrb_define_const(mrb, mBrotli, "MODE_FONT", VALUE((mrb_int)BROTLI_MODE_FONT));
    mrb_define_const(mrb, mBrotli, "DEFAULT_QUALITY", VALUE((mrb_int)BROTLI_DEFAULT_QUALITY));
    mrb_define_const(mrb, mBrotli, "DEFAULT_WINDOW", VALUE((mrb_int)BROTLI_DEFAULT_WINDOW));
    mrb_define_const(mrb, mBrotli, "DEFAULT_MODE", VALUE((mrb_int)BROTLI_DEFAULT_MODE));
}


/* class Brotli::Encoder */

struct encoder
{
    BrotliEncoderState *brotli;
    VALUE outport;
    struct RString *outbuf;
    struct {
        char *ptr;
        char *cur;
        size_t size;
    } inbuf;
    uint64_t total_in;
    uint64_t total_out;
};

static void
encoder_free(MRB, struct encoder *p)
{
    if (p->brotli) {
        BrotliEncoderDestroyInstance(p->brotli);
        p->brotli = NULL;
    }

    if (p->inbuf.ptr) {
        mrb_free(mrb, p->inbuf.ptr);
        memset(&p->inbuf, 0, sizeof(p->inbuf));
    }

    if (p) {
        mrb_free(mrb, p);
    }
}

static const mrb_data_type encoder_type = {
    .struct_name = "encoder@mruby-brotli",
    .dfree = (void (*)(mrb_state *, void *))encoder_free,
};

static struct encoder *
getencoder(MRB, VALUE self)
{
    return (struct encoder *)mrbx_getref(mrb, self, &encoder_type);
}

static VALUE
encoder_set_outport(MRB, VALUE self, struct encoder *p, VALUE port)
{
    mrb_iv_set(mrb, self, SYMBOL("outport@mruby-brotli"), port);
    p->outport = port;

    return port;
}

static struct RString *
encoder_set_outbuf(MRB, VALUE obj, struct encoder *p, struct RString *buf)
{
    mrb_iv_set(mrb, obj, SYMBOL("outbuf@mruby-brotli"), (buf ? VALUE(buf) : Qnil));
    p->outbuf = buf;

    return buf;
}

/*
 * call-seq:
 *  new(outbuf) -> encoder object
 *  new(outbuf, quality: nil, lgwin: nil, mode: nil, sizehint: nil) -> encoder object
 */
static VALUE
enc_s_new(MRB, VALUE self)
{
    struct RData *rd;
    struct encoder *p;
    Data_Make_Struct(mrb, mrb_class_ptr(self), struct encoder, &encoder_type, p, rd);

    p->brotli = BrotliEncoderCreateInstance((brotli_alloc_func)mrb_malloc_simple, (brotli_free_func)mrb_free, mrb);

    if (!p->brotli) {
        mrb_free(mrb, rd->data);
        rd->data = NULL;
        mrb_raise(mrb, E_RUNTIME_ERROR,
                  "failed allocation in BrotliEncoderCreateInstance()");
    }

    p->outport = Qnil;
    p->outbuf = NULL;
    p->total_in = 0;
    p->total_out = 0;
    p->inbuf.ptr = mrb_malloc(mrb, EXT_INBUF_SIZE);
    p->inbuf.cur = p->inbuf.ptr;
    p->inbuf.size = EXT_INBUF_SIZE;

    VALUE obj = VALUE(rd);

    mrbx_funcall_passthrough(mrb, obj, id_initialize);

    return obj;
}

static void
enc_initialize_args(MRB, VALUE self, struct encoder **p, VALUE *outport)
{
    VALUE opts = Qnil;
    mrb_get_args(mrb, "o|H", outport, &opts);

    *p = getencoder(mrb, self);

    if (!NIL_P(opts)) {
        VALUE quality, lgwin, mode, size_hint;
        MRBX_SCANHASH(mrb, opts, Qnil,
                MRBX_SCANHASH_ARGS("quality", &quality, Qnil),
                MRBX_SCANHASH_ARGS("lgwin", &lgwin, Qnil),
                MRBX_SCANHASH_ARGS("mode", &mode, Qnil),
                MRBX_SCANHASH_ARGS("size_hint", &size_hint, Qnil));

        if (!NIL_P(quality)) {
            BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_QUALITY, convert_to_quality(mrb, quality));
        }

        if (!NIL_P(lgwin)) {
            BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_LGWIN, convert_to_lgwin(mrb, lgwin));
        }

        if (!NIL_P(mode)) {
            BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_MODE, convert_to_mode(mrb, mode));
        }

        if (!NIL_P(size_hint)) {
            BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_SIZE_HINT, mrb_int(mrb, size_hint));
        }

        //BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_LGBLOCK, ?);
        //BrotliEncoderSetParameter((*p)->brotli, BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING, TRUE);
    }
}

static VALUE
enc_initialize(MRB, VALUE self)
{
    struct encoder *p;
    VALUE outport;
    enc_initialize_args(mrb, self, &p, &outport);

    encoder_set_outport(mrb, self, p, outport);
    encoder_set_outbuf(mrb, self, p, NULL);

    return self;
}

static void
enc_update(MRB, VALUE self, struct encoder *p,
           const char *next_in, size_t avail_in,
           BrotliEncoderOperation op)
{
    size_t insize = avail_in;

    for (;;) {
        encoder_set_outbuf(mrb, self, p, mrbx_str_recycle(mrb, p->outbuf, EXT_DEFAULT_OUTBUF_SIZE));
        mrbx_str_set_len(mrb, p->outbuf, 0);

        char *next_out = RSTR_PTR(p->outbuf);
        size_t avail_out = RSTR_CAPA(p->outbuf);

        BROTLI_BOOL ok = BrotliEncoderCompressStream(p->brotli, op,
                                                     &avail_in, (const uint8_t **)&next_in,
                                                     &avail_out, (uint8_t **)&next_out, &p->total_out);

        if (!ok) {
            mrb_raisef(mrb, E_RUNTIME_ERROR,
                       "failed BrotliEncoderCompressStream - %S", self);
        }

        size_t size = next_out - RSTR_PTR(p->outbuf);

        mrbx_str_set_len(mrb, p->outbuf, size);

        if (size > 0) {
            FUNCALL(mrb, p->outport, id_op_lsh, VALUE(p->outbuf));
        }

        if (avail_in > 0 || BrotliEncoderHasMoreOutput(p->brotli)) {
            continue;
        } else {
            break;
        }
    }

    p->total_in += insize;
}

/*
 * call-seq:
 *  encode(src) -> self
 */
static VALUE
enc_encode(MRB, VALUE self)
{
    VALUE src;
    mrb_get_args(mrb, "S", &src);

    enc_update(mrb, self, getencoder(mrb, self),
               RSTRING_PTR(src), RSTRING_LEN(src),
               BROTLI_OPERATION_PROCESS);

    return self;
}

static VALUE
enc_flush(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    struct encoder *p = getencoder(mrb, self);

    enc_update(mrb, self, p, NULL, 0, BROTLI_OPERATION_FLUSH);

    return self;
}

static VALUE
enc_finish(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    enc_update(mrb, self, getencoder(mrb, self), NULL, 0, BROTLI_OPERATION_FINISH);

    return self;
}

static VALUE
enc_is_finished(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    struct encoder *p = getencoder(mrb, self);

    return (BrotliEncoderIsFinished(p->brotli) == BROTLI_FALSE ? Qfalse : Qtrue);
}

static VALUE
enc_total_in(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    struct encoder *p = getencoder(mrb, self);

    if (p->total_in > MRB_INT_MAX) {
        return mrb_float_value(mrb, p->total_in);
    } else {
        return mrb_fixnum_value(p->total_in);
    }
}

static VALUE
enc_total_out(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    struct encoder *p = getencoder(mrb, self);

    if (p->total_out > MRB_INT_MAX) {
        return mrb_float_value(mrb, p->total_out);
    } else {
        return mrb_fixnum_value(p->total_out);
    }
}

static void
enc_s_encode_args(MRB, VALUE self, struct RString **input, size_t *insize, struct RString **output, size_t *outsize, int *quality, int *lgwin, int *mode)
{
    VALUE *argv = NULL;
    mrb_int argc = 0;
    mrb_get_args(mrb, "*", &argv, &argc);

    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        VALUE quality_v, lgwin_v, mode_v;

        MRBX_SCANHASH(mrb, argv[argc - 1], Qnil,
                MRBX_SCANHASH_ARGS("quality", &quality_v, Qnil),
                MRBX_SCANHASH_ARGS("lgwin", &lgwin_v, Qnil),
                MRBX_SCANHASH_ARGS("mode", &mode_v, Qnil));

        *quality = convert_to_quality(mrb, quality_v);
        *lgwin = convert_to_lgwin(mrb, lgwin_v);
        *mode = convert_to_mode(mrb, mode_v);

        argc --;
    } else {
        *quality = BROTLI_DEFAULT_QUALITY;
        *lgwin = BROTLI_DEFAULT_WINDOW;
        *mode = BROTLI_DEFAULT_MODE;
    }

    switch (argc) {
    case 1:
        *outsize = -1;
        *output = NULL;
        break;
    case 2:
        if (mrb_string_p(argv[1])) {
            *outsize = -1;
            *output = RString(argv[1]);
        } else {
            *outsize = convert_to_size_t(mrb, argv[1]);
            *output = NULL;
        }
        break;
    case 3:
        *outsize = convert_to_size_t(mrb, argv[1]);
        *output = RString(argv[2]);
        break;
    default:
        mrb_raisef(mrb, E_ARGUMENT_ERROR,
                   "wrong number arguments (given %S, expect 1 .. 2 + keywords)",
                   VALUE(argc));
    }

    mrb_check_type(mrb, argv[0], MRB_TT_STRING);
    *input = RSTRING(argv[0]);
    *insize = RSTR_LEN(*input);

    if ((ssize_t)*outsize < 0) {
        *outsize = BrotliEncoderMaxCompressedSize(*insize);
    }

    *output = mrbx_str_force_recycle(mrb, *output, *outsize);
    mrbx_str_set_len(mrb, *output, 0);
}

/*
 * call-seq:
 *  encode(input, outsize = nil, output = nil, **opts) -> output
 *  encode(input, output, **opts) -> output
 *
 * [input]
 * [output = nil]
 * [outsize = nil]
 * [opts]
 *  quality = nil::
 *  lgwin = nil::
 *  mode = nil::
 */
static VALUE
enc_s_encode(MRB, VALUE self)
{
    struct RString *input, *output;
    size_t insize, outsize;
    int quality, lgwin, mode;
    enc_s_encode_args(mrb, self, &input, &insize, &output, &outsize, &quality, &lgwin, &mode);

    BROTLI_BOOL ok = BrotliEncoderCompress(
            quality, lgwin, mode,
            insize, (const uint8_t *)RSTR_PTR(input),
            &outsize, (uint8_t *)RSTR_PTR(output));

    if (!ok) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "failed BrotliEncoderCompress");
    }

    mrbx_str_set_len(mrb, output, outsize);

    return VALUE(output);
}

static void
init_encoder(MRB, struct RClass *mBrotli)
{
    struct RClass *cEncoder = mrb_define_class_under(mrb, mBrotli, "Encoder", mrb_cObject);
    mrb_define_class_method(mrb, cEncoder, "encode", enc_s_encode, MRB_ARGS_ANY());
    mrb_define_class_method(mrb, cEncoder, "new", enc_s_new, MRB_ARGS_ANY());
    mrb_define_method(mrb, cEncoder, "initialize", enc_initialize, MRB_ARGS_ANY());
    mrb_define_method(mrb, cEncoder, "encode", enc_encode, MRB_ARGS_ANY());
    mrb_define_method(mrb, cEncoder, "flush", enc_flush, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "finish", enc_finish, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "finished?", enc_is_finished, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "total_in", enc_total_in, MRB_ARGS_NONE());
    mrb_define_method(mrb, cEncoder, "total_out", enc_total_out, MRB_ARGS_NONE());
    //mrb_define_method(mrb, cEncoder, "outport", enc_get_outport, MRB_ARGS_NONE());
    //mrb_define_method(mrb, cEncoder, "outport=", enc_set_outport, MRB_ARGS_ARG(1));
}

/* class Brotli::Decoder */

struct decoder
{
    BrotliDecoderState *brotli;
    VALUE inport;
    const char *nextin;
    size_t availin;
    size_t total_out;
    BrotliDecoderResult status;
};

static void
decoder_free(MRB, struct decoder *p)
{
    if (p->brotli) {
        BrotliDecoderDestroyInstance(p->brotli);
        p->brotli = NULL;
    }

    if (p) {
        mrb_free(mrb, p);
    }
}

static const mrb_data_type decoder_type = {
    .struct_name = "decoder@mruby-brotli",
    .dfree = (void (*)(mrb_state *, void *))decoder_free,
};

static struct decoder *
getdecoder(MRB, VALUE self)
{
    return (struct decoder *)mrbx_getref(mrb, self, &decoder_type);
}

/*
 * call-seq:
 *  new(inport) -> decoder object
 */
static VALUE
dec_s_new(MRB, VALUE self)
{
    struct RData *rd = mrb_data_object_alloc(mrb, RClass(self), NULL, &decoder_type);
    rd->data = mrb_calloc(mrb, sizeof(struct decoder), 1);
    struct decoder *p = (struct decoder *)rd->data;
    p->brotli = BrotliDecoderCreateInstance((brotli_alloc_func)mrb_malloc_simple, (brotli_free_func)mrb_free, mrb);
    if (!p->brotli) {
        mrb_free(mrb, rd->data);
        rd->data = NULL;
        mrb_raise(mrb, E_RUNTIME_ERROR,
                  "allocation error in BrotliDecoderCreateInstance()");
    }

    //BrotliDecoderSetParameter(p->brotli, BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION, BROTLI_TRUE);

    p->inport = Qnil;
    p->total_out = 0;
    p->status = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;

    VALUE obj = VALUE(rd);

    mrbx_funcall_passthrough(mrb, obj, id_initialize);

    return obj;
}

static VALUE
dec_initialize(MRB, VALUE self)
{
    struct decoder *p = getdecoder(mrb, self);

    VALUE inport;
    mrb_get_args(mrb, "o", &inport);

    p->inport = mrbx_fakedin_new(mrb, inport);
    p->status = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;

    return self;
}

static void
dec_decode_args(MRB, VALUE self, ssize_t *size, struct RString **dest)
{
    VALUE argv[] = { Qnil, Qnil };
    mrb_int argc = mrb_get_args(mrb, "|oS!", &argv[0], &argv[1]);

    (void)argc;

    if (NIL_P(argv[0])) {
        *size = -1;
    } else {
        mrb_int n = mrb_int(mrb, argv[0]);
        if (n < 0 || n > SSIZE_MAX) {
            mrb_raisef(mrb, E_ARGUMENT_ERROR,
                       "``size'' is too big or too small - %S",
                       argv[0]);
        }
        *size = n;
    }

    *dest = mrbx_str_force_recycle(mrb, argv[1], (*size < 0 ? EXT_PARTIAL_READ_SIZE : *size));
    mrbx_str_set_len(mrb, *dest, 0);
}

static ssize_t
dec_decode_partial(MRB, VALUE self, struct decoder *p, char *dest, ssize_t size)
{
    intptr_t dest0 = (intptr_t)dest;
    intptr_t destend = (intptr_t)dest + size;

    if (p->status <= BROTLI_DECODER_RESULT_SUCCESS) { return 0; }

    while ((intptr_t)dest < destend) {
        if (p->status == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
            p->availin = (size_t)mrbx_fakedin_read(mrb, p->inport, &p->nextin, EXT_PARTIAL_READ_SIZE);
        }

        if ((mrb_int)p->availin < 0) {
            mrb_raise(mrb, E_RUNTIME_ERROR, "unexpected end of stream");
        }

        p->status = BrotliDecoderDecompressStream(p->brotli, &p->availin, (const uint8_t **)&p->nextin, (size_t *)&size, (uint8_t **)&dest, &p->total_out);

        if (p->status == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        } else if (p->status < BROTLI_DECODER_RESULT_SUCCESS) {
            int err = BrotliDecoderGetErrorCode(p->brotli);
            mrb_raisef(mrb, E_RUNTIME_ERROR,
                       "failed BrotliDecoderDecompressStream() - %S (%S)",
                       VALUE((mrb_int)err),
                       VALUE(BrotliDecoderErrorString(err)));
        }
    }

    return (intptr_t)dest - dest0;
}

struct dec_decode_full_growup
{
    VALUE self;
    struct decoder *decoder;
};

static ssize_t
dec_decode_full_growup(MRB, char *ptr, size_t *size, void *user)
{
    struct dec_decode_full_growup *argp = user;

    *size = dec_decode_partial(mrb, argp->self, argp->decoder, ptr, *size);

    if (*size == 0) {
        return MRBX_STOP;
    } else {
        return MRBX_NEXT;
    }
}

static ssize_t
dec_decode_full(MRB, VALUE self, struct decoder *p, struct RString *dest)
{
    struct dec_decode_full_growup args = { self, p };

    mrbx_str_buf_growup(mrb, dest, -1, NULL, dec_decode_full_growup, &args);

    return RSTR_LEN(dest);
}

static VALUE
dec_decode(MRB, VALUE self)
{
    struct decoder *p = getdecoder(mrb, self);

    ssize_t size;
    struct RString *dest;
    dec_decode_args(mrb, self, &size, &dest);

    if (size == 0) {
        return VALUE(dest);
    }

    if (p->status <= BROTLI_DECODER_RESULT_SUCCESS) {
        return Qnil;
    }

    if (size < 0) {
        size = dec_decode_full(mrb, self, p, dest);
    } else {
        size = dec_decode_partial(mrb, self, p, RSTR_PTR(dest), size);
        mrbx_str_set_len(mrb, dest, size);
    }

    if (size > 0) {
        return VALUE(dest);
    } else {
        return Qnil;
    }
}

static VALUE
dec_finish(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    getdecoder(mrb, self)->status = BROTLI_DECODER_RESULT_SUCCESS;

    return Qnil;
}

static VALUE
dec_is_finished(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    return (getdecoder(mrb, self)->status <= BROTLI_DECODER_RESULT_SUCCESS ? Qtrue : Qfalse);
}

static VALUE
dec_total_in(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    return mrb_fixnum_value(mrbx_fakedin_total_in(mrb, getdecoder(mrb, self)->inport));
}

static VALUE
dec_total_out(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    return mrb_fixnum_value(getdecoder(mrb, self)->total_out);
}

static VALUE
dec_get_inport(MRB, VALUE self)
{
    mrb_get_args(mrb, "");

    return mrbx_fakedin_stream(mrb, getdecoder(mrb, self)->inport);
}

static void
dec_s_decode_args(MRB, VALUE self, struct RString **input, struct RString **output, size_t *outsize, mrb_bool *partial)
{
    mrb_int argc;
    VALUE *argv;
    mrb_get_args(mrb, "*", &argv, &argc);

    VALUE is_partial;
    if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
        MRBX_SCANHASH(mrb, argv[argc - 1], Qfalse,
                MRBX_SCANHASH_ARG("partial", &is_partial, Qnil));

        argc --;
    } else {
        is_partial = Qnil;
    }

    switch (argc) {
    case 1:
        *outsize = (size_t)-1;
        *output = NULL;
        break;
    case 2:
        if (mrb_string_p(argv[1])) {
            *outsize = (size_t)-1;
            *output = RString(argv[1]);
        } else {
            *outsize = convert_to_size_t(mrb, argv[1]);
            *output = NULL;
        }
        break;
    case 3:
        *outsize = convert_to_size_t(mrb, argv[1]);
        *output = RString(argv[2]);
        break;
    default:
        mrb_raisef(mrb, E_ARGUMENT_ERROR,
                   "wrong number arguments (given %S, expect 1 .. 3)",
                   VALUE(argc));
    }

    mrb_check_type(mrb, argv[0], MRB_TT_STRING);
    *input = RSTRING(argv[0]);

    if ((ssize_t)*outsize < 0) {
        *output = mrbx_str_force_recycle(mrb, *output, EXT_PARTIAL_READ_SIZE);
        *partial = RTEST(is_partial);
    } else {
        *output = mrbx_str_force_recycle(mrb, *output, *outsize);
        *partial = NIL_P(is_partial) || RTEST(is_partial);
    }

    mrbx_str_set_len(mrb, *output, 0);
}

static void
dec_s_decode_partial(MRB, const char *input, size_t insize, struct RString *output, size_t outsize, mrb_bool partial)
{
    BrotliDecoderState *brotli;
    brotli = BrotliDecoderCreateInstance((brotli_alloc_func)mrb_malloc_simple, (brotli_free_func)mrb_free, mrb);
    if (!brotli)
    {
        mrb_raise(mrb, E_RUNTIME_ERROR,
                  "failed BrotliDecoderCreateInstance (may be out of memory)");
    }

    char *outp = RSTR_PTR(output);
    size_t availout = outsize;
    BrotliDecoderResult ok = BrotliDecoderDecompressStream(brotli, &insize, (const uint8_t **)&input, &availout, (uint8_t **)&outp, NULL);
    BrotliDecoderDestroyInstance(brotli);

    if (ok != BROTLI_DECODER_RESULT_SUCCESS &&
            !(partial && ok == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)) {
        aux_brotli_decoder_error(mrb, ok);
    }

    mrbx_str_set_len(mrb, output, outsize - availout);
}

struct dec_s_decode_full_growup
{
    BrotliDecoderState *brotli;
    const char *input;
    size_t insize;
    struct RString *output;
    mrb_bool partial;
};

static ssize_t
dec_s_decode_full_growup(MRB, char *buf, size_t *size, void *user)
{
    struct dec_s_decode_full_growup *argp = user;
    size_t outsize = *size;

    BrotliDecoderResult ok;
    ok = BrotliDecoderDecompressStream(argp->brotli, &argp->insize, (const uint8_t **)&argp->input, &outsize, (uint8_t **)&buf, NULL);
    *size = *size - outsize;

    if (ok == BROTLI_DECODER_RESULT_SUCCESS) {
        return MRBX_STOP;
    }

    if (ok != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
        aux_brotli_decoder_error(mrb, ok);
    }

    return MRBX_NEXT;
}

static VALUE
dec_s_decode_full_try(MRB, VALUE args)
{
    struct dec_s_decode_full_growup *argp = mrb_cptr(args);

    mrbx_str_buf_growup(mrb, argp->output, -1, &argp->partial, dec_s_decode_full_growup, argp);

    return Qnil;
}

static VALUE
dec_s_decode_full_cleanup(MRB, VALUE args)
{
    struct dec_s_decode_full_growup *argp = mrb_cptr(args);

    BrotliDecoderDestroyInstance(argp->brotli);

    return Qnil;
}

static void
dec_s_decode_full(MRB, const char *input, size_t insize, struct RString *output, mrb_bool partial)
{
    struct dec_s_decode_full_growup args = {
        BrotliDecoderCreateInstance((brotli_alloc_func)mrb_malloc_simple, (brotli_free_func)mrb_free, mrb),
        input,
        insize,
        output,
        partial,
    };

    if (!args.brotli) {
        mrb_raise(mrb, E_RUNTIME_ERROR,
                  "failed BrotliDecoderCreateInstance (may be out of memory)");
    }

    mrb_ensure(mrb,
               dec_s_decode_full_try, mrb_cptr_value(mrb, &args),
               dec_s_decode_full_cleanup, mrb_cptr_value(mrb, &args));
}

/*
 * call-seq:
 *  decode(input, outsize = nil, output = nil, partial: nil) -> output
 *  decode(input, output, partial: nil) -> output
 */
static VALUE
dec_s_decode(MRB, VALUE self)
{
    struct RString *input, *output;
    size_t outsize;
    mrb_bool partial;
    dec_s_decode_args(mrb, self, &input, &output, &outsize, &partial);

    if ((ssize_t)outsize < 0) {
        dec_s_decode_full(mrb, RSTR_PTR(input), RSTR_LEN(input), output, partial);
    } else {
        dec_s_decode_partial(mrb, RSTR_PTR(input), RSTR_LEN(input), output, outsize, partial);
    }

    return VALUE(output);
}

static void
init_decoder(MRB, struct RClass *mBrotli)
{
    struct RClass *cDecoder = mrb_define_class_under(mrb, mBrotli, "Decoder", mrb_cObject);
    mrb_define_class_method(mrb, cDecoder, "decode", dec_s_decode, MRB_ARGS_ANY());
    mrb_define_class_method(mrb, cDecoder, "new", dec_s_new, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "initialize", dec_initialize, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "decode", dec_decode, MRB_ARGS_ANY());
    mrb_define_method(mrb, cDecoder, "finish", dec_finish, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "finished?", dec_is_finished, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "total_in", dec_total_in, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "total_out", dec_total_out, MRB_ARGS_NONE());
    mrb_define_method(mrb, cDecoder, "inport", dec_get_inport, MRB_ARGS_NONE());
    //mrb_define_method(mrb, cDecoder, "inport=", dec_set_inport, MRB_ARGS_ARG(1));
}

/* module Brotli */

void
mrb_mruby_brotli_gem_init(MRB)
{
    struct RClass *mBrotli = mrb_define_module(mrb, "Brotli");

    init_constants(mrb, mBrotli);
    init_encoder(mrb, mBrotli);
    init_decoder(mrb, mBrotli);
}

void
mrb_mruby_brotli_gem_final(MRB)
{
}
