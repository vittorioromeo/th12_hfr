/* The filter shader contract, in one place: the prologue every filter is compiled against,
 * and the rules for splitting a file into passes. The runtime and the tool that checks
 * shaders before shipping them both include this, so the tool cannot drift from what the
 * game actually does -- which it had, before this header existed.
 *
 * Nothing here touches Direct3D, so the tool can use it on any host.
 */
#ifndef HFR_SHADER_PARSE_H
#define HFR_SHADER_PARSE_H

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Prepended to every pass. A filter defines float4 main(float2 uv : TEXCOORD0) : COLOR0.
   The quad is drawn with the Direct3D 9 half-pixel offset, so uv arrives at output-pixel
   centres: for a target that is an exact multiple of the source, frac(uv * SourceSize.xy)
   identifies the sub-pixel. Fixed-scale filters such as MMPX depend on that. */
static const char SHADER_PROLOGUE[] =
    "sampler2D Source : register(s0);\n"    /* the previous pass; the game's image, in pass 0 */
    "sampler2D Original : register(s1);\n"  /* always the game's own image                    */
    "sampler2D Pass0 : register(s2);\n"     /* an earlier pass's output, for passes that       */
    "sampler2D Pass1 : register(s3);\n"     /* reach further back than the one before them     */
    "sampler2D Pass2 : register(s4);\n"
    "sampler2D Pass3 : register(s5);\n"
    "sampler2D Pass4 : register(s6);\n"
    "sampler2D Pass5 : register(s7);\n"
    "float4 SourceSize : register(c0);\n"   /* w, h, 1/w, 1/h of the image being read    */
    "float4 TargetSize : register(c1);\n"   /* w, h, 1/w, 1/h of the surface being drawn */
    "float4 OriginalSize : register(c2);\n" /* w, h, 1/w, 1/h of the game's own image    */
    "float4 Params : register(c3);\n"       /* x = strength 0..1 for a post-process; else 0 */
    "#define SourceSampler Source\n"
    "#line 1\n";

#define HFR_MAX_PASSES 8
#define HFR_MAX_TOTAL_SCALE 16

struct ShaderPass {
    const char* body;       /* into the text passed to shader_split, which must outlive it */
    size_t      len;
    int         scale;      /* output size = scale x this pass's input */
    int         want_float; /* writes values outside 0..1, so it needs a float target */
};

/* Directives are "//!" followed by a word, one per line, anywhere in a shader:
     //! pass          begins a new pass (everything before the first one is a shared header)
     //! scale N       this pass's output is N times the size of its input; 1 by default
     //! float         this pass writes values outside 0..1, so it needs a float target
     //! post          not a filter but a post-process: one pass run over the finished,
                       window-sized image (sharpening), with Params.x as its strength
   A file with no directives at all is a single free-scale pass drawn straight to the
   destination, which is what every shader written before passes existed relies on. */
static const char* shader_next_directive(const char* p, const char** word, const char** rest) {
    while ((p = strstr(p, "//!")) != NULL) {
        const char* q = p + 3;
        while (*q == ' ' || *q == '\t') ++q;
        if (isalpha((unsigned char)*q)) {
            *word = q;
            while (isalpha((unsigned char)*q)) ++q;
            while (*q == ' ' || *q == '\t') ++q;
            *rest = q;
            return p;
        }
        p += 3;
    }
    return NULL;
}
static int shader_directive_is(const char* word, const char* name) {
    size_t n = strlen(name);
    return !strncmp(word, name, n) && !isalpha((unsigned char)word[n]);
}
/* Whether the file declares itself a post-process ("//! post" anywhere in it). */
static int shader_is_post(const char* text) {
    const char *p = text, *word, *rest;
    while ((p = shader_next_directive(p, &word, &rest)) != NULL) {
        if (shader_directive_is(word, "post")) return 1;
        p += 3;
    }
    return 0;
}
static void shader_read_flags(const char* from, const char* to, struct ShaderPass* out) {
    const char *q = from, *word, *rest;
    while ((q = shader_next_directive(q, &word, &rest)) != NULL && q < to) {
        if (shader_directive_is(word, "scale")) {
            int v = atoi(rest);
            if (v >= 1 && v <= 8) out->scale = v;
        }
        if (shader_directive_is(word, "float")) out->want_float = 1;
        q += 3;
    }
}

/* Splits text into passes. Returns the pass count, or 0 with *err set to why.
   total_scale is the product of the pass scales, or 0 for a single free-scale pass. */
static int shader_split(const char* text, const char** header, size_t* header_len,
                        struct ShaderPass* passes, int* total_scale, const char** err) {
    const char *p = text, *word, *rest;
    const char* starts[HFR_MAX_PASSES];
    int n = 0;
    size_t text_len = strlen(text);

    *err = NULL;
    *header = text;
    *header_len = text_len;

    while ((p = shader_next_directive(p, &word, &rest)) != NULL) {
        if (shader_directive_is(word, "pass")) {
            if (n >= HFR_MAX_PASSES) { *err = "more passes than the runtime allows"; return 0; }
            if (n == 0) *header_len = (size_t)(p - text);
            starts[n++] = p;
        }
        p += 3;
    }

    if (n == 0) {                       /* one pass, and the whole file is its body */
        *header_len = 0;
        memset(&passes[0], 0, sizeof passes[0]);
        passes[0].body = text;
        passes[0].len = text_len;
        shader_read_flags(text, text + text_len, &passes[0]);
        *total_scale = passes[0].scale;   /* 0 unless "//! scale" said otherwise */
        return 1;
    }
    /* Several passes: each runs at a fixed size, so the chain as a whole magnifies by a
       known amount and the last resample to the window is an ordinary sampled blit. */
    *total_scale = 1;
    for (int i = 0; i < n; ++i) {
        memset(&passes[i], 0, sizeof passes[i]);
        passes[i].body = starts[i];
        passes[i].len  = (size_t)((i + 1 < n ? starts[i + 1] : text + text_len) - starts[i]);
        passes[i].scale = 1;
        shader_read_flags(passes[i].body, passes[i].body + passes[i].len, &passes[i]);
        if (*total_scale * passes[i].scale > HFR_MAX_TOTAL_SCALE) {
            *err = "magnifies more than the runtime allows";
            return 0;
        }
        *total_scale *= passes[i].scale;
    }
    return n;
}

#endif
