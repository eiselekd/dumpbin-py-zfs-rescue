/*
 * Copyright (c) 2012-2013, Steeve Morin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Steeve Morin nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <Python.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "python-lz4.h"


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


/*
 * Macros to read unaligned values from a specific byte order to
 * native byte order
 */

#define	BE_IN8(xa) \
	*((uint8_t *)(xa))

#define	BE_IN16(xa) \
	(((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define	BE_IN32(xa) \
	(((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))

#define	BE_IN64(xa) \
	(((uint64_t)BE_IN32(xa) << 32) | BE_IN32((uint8_t *)(xa)+4))

#define	LE_IN8(xa) \
	*((uint8_t *)(xa))

#define	LE_IN16(xa) \
	(((uint16_t)LE_IN8((uint8_t *)(xa) + 1) << 8) | LE_IN8(xa))

#define	LE_IN32(xa) \
	(((uint32_t)LE_IN16((uint8_t *)(xa) + 2) << 16) | LE_IN16(xa))

#define	LE_IN64(xa) \
	(((uint64_t)LE_IN32((uint8_t *)(xa) + 4) << 32) | LE_IN32(xa))

/*
 * LZ4 - Fast LZ compression algorithm
 * Header File
 * Copyright (C) 2011-2013, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
 * - LZ4 source repository : http://code.google.com/p/lz4/
 */

//#include <sys/zfs_context.h>

static int real_LZ4_compress(const char *source, char *dest, int isize,
    int osize);
static int LZ4_uncompress_unknownOutputSize(const char *source, char *dest,
    int isize, int maxOutputSize);
static int LZ4_compressCtx(void *ctx, const char *source, char *dest,
    int isize, int osize);
static int LZ4_compress64kCtx(void *ctx, const char *source, char *dest,
    int isize, int osize);


/*ARGSUSED*/
int
lz4_decompress_zfs(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	const char *src = s_start;
	uint32_t bufsiz = BE_IN32(src);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len)
		return (1);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	int l = LZ4_uncompress_unknownOutputSize(&src[sizeof (bufsiz)],
						 d_start, bufsiz, d_len);
	printf("Decoded %d\n", l);
	return (l < 0);
}

/*
 * LZ4 API Description:
 *
 * Simple Functions:
 * real_LZ4_compress() :
 * 	isize  : is the input size. Max supported value is ~1.9GB
 * 	return : the number of bytes written in buffer dest
 *		 or 0 if the compression fails (if LZ4_COMPRESSMIN is set).
 * 	note : destination buffer must be already allocated.
 * 		destination buffer must be sized to handle worst cases
 * 		situations (input data not compressible) worst case size
 * 		evaluation is provided by function LZ4_compressBound().
 *
 * real_LZ4_uncompress() :
 * 	osize  : is the output size, therefore the original size
 * 	return : the number of bytes read in the source buffer.
 * 		If the source stream is malformed, the function will stop
 * 		decoding and return a negative result, indicating the byte
 * 		position of the faulty instruction. This function never
 * 		writes beyond dest + osize, and is therefore protected
 * 		against malicious data packets.
 * 	note : destination buffer must be already allocated
 *	note : real_LZ4_uncompress() is not used in ZFS so its code
 *	       is not present here.
 *
 * Advanced Functions
 *
 * LZ4_compressBound() :
 * 	Provides the maximum size that LZ4 may output in a "worst case"
 * 	scenario (input data not compressible) primarily useful for memory
 * 	allocation of output buffer.
 *
 * 	isize  : is the input size. Max supported value is ~1.9GB
 * 	return : maximum output size in a "worst case" scenario
 * 	note : this function is limited by "int" range (2^31-1)
 *
 * LZ4_uncompress_unknownOutputSize() :
 * 	isize  : is the input size, therefore the compressed size
 * 	maxOutputSize : is the size of the destination buffer (which must be
 * 		already allocated)
 * 	return : the number of bytes decoded in the destination buffer
 * 		(necessarily <= maxOutputSize). If the source stream is
 * 		malformed, the function will stop decoding and return a
 * 		negative result, indicating the byte position of the faulty
 * 		instruction. This function never writes beyond dest +
 * 		maxOutputSize, and is therefore protected against malicious
 * 		data packets.
 * 	note   : Destination buffer must be already allocated.
 *		This version is slightly slower than real_LZ4_uncompress()
 *
 * LZ4_compressCtx() :
 * 	This function explicitly handles the CTX memory structure.
 *
 * 	ILLUMOS CHANGES: the CTX memory structure must be explicitly allocated
 * 	by the caller (either on the stack or using kmem_cache_alloc). Passing
 * 	NULL isn't valid.
 *
 * LZ4_compress64kCtx() :
 * 	Same as LZ4_compressCtx(), but specific to small inputs (<64KB).
 * 	isize *Must* be <64KB, otherwise the output will be corrupted.
 *
 * 	ILLUMOS CHANGES: the CTX memory structure must be explicitly allocated
 * 	by the caller (either on the stack or using kmem_cache_alloc). Passing
 * 	NULL isn't valid.
 */

/*
 * Tuning parameters
 */

/*
 * COMPRESSIONLEVEL: Increasing this value improves compression ratio
 *	 Lowering this value reduces memory usage. Reduced memory usage
 *	typically improves speed, due to cache effect (ex: L1 32KB for Intel,
 *	L1 64KB for AMD). Memory usage formula : N->2^(N+2) Bytes
 *	(examples : 12 -> 16KB ; 17 -> 512KB)
 */
#define	COMPRESSIONLEVEL 12

/*
 * NOTCOMPRESSIBLE_CONFIRMATION: Decreasing this value will make the
 *	algorithm skip faster data segments considered "incompressible".
 *	This may decrease compression ratio dramatically, but will be
 *	faster on incompressible data. Increasing this value will make
 *	the algorithm search more before declaring a segment "incompressible".
 *	This could improve compression a bit, but will be slower on
 *	incompressible data. The default value (6) is recommended.
 */
#define	NOTCOMPRESSIBLE_CONFIRMATION 6

/*
 * BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE: This will provide a boost to
 * performance for big endian cpu, but the resulting compressed stream
 * will be incompatible with little-endian CPU. You can set this option
 * to 1 in situations where data will stay within closed environment.
 * This option is useless on Little_Endian CPU (such as x86).
 */
/* #define	BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE 1 */

/*
 * CPU Feature Detection
 */

/* 32 or 64 bits ? */
#if defined(_LP64)
#define	LZ4_ARCH64 1
#else
#define	LZ4_ARCH64 0
#endif

/*
 * Little Endian or Big Endian?
 * Note: overwrite the below #define if you know your architecture endianess.
 */
#if defined(_BIG_ENDIAN)
#define	LZ4_BIG_ENDIAN 1
#else
/*
 * Little Endian assumed. PDP Endian and other very rare endian format
 * are unsupported.
 */
#undef LZ4_BIG_ENDIAN
#endif

/*
 * Unaligned memory access is automatically enabled for "common" CPU,
 * such as x86. For others CPU, the compiler will be more cautious, and
 * insert extra code to ensure aligned access is respected. If you know
 * your target CPU supports unaligned memory access, you may want to
 * force this option manually to improve performance
 */
#if defined(__ARM_FEATURE_UNALIGNED)
#define	LZ4_FORCE_UNALIGNED_ACCESS 1
#endif

/*
 * Illumos : we can't use GCC's __builtin_ctz family of builtins in the
 * kernel
 * Linux : we can use GCC's __builtin_ctz family of builtins in the
 * kernel
 */
#undef	LZ4_FORCE_SW_BITCOUNT
#if defined(__sparc)
#define	LZ4_FORCE_SW_BITCOUNT
#endif

/*
 * Compiler Options
 */
/* Disable restrict */
#define	restrict

/*
 * Linux : GCC_VERSION is defined as of 3.9-rc1, so undefine it.
 * torvalds/linux@3f3f8d2f48acfd8ed3b8e6b7377935da57b27b16
 */
#ifdef GCC_VERSION
#undef GCC_VERSION
#endif

#define	GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#if (GCC_VERSION >= 302) || (__INTEL_COMPILER >= 800) || defined(__clang__)
#define	expect(expr, value)    (__builtin_expect((expr), (value)))
#else
#define	expect(expr, value)    (expr)
#endif

#ifndef likely
#define	likely(expr)	expect((expr) != 0, 1)
#endif

#ifndef unlikely
#define	unlikely(expr)	expect((expr) != 0, 0)
#endif

#define	lz4_bswap16(x) ((unsigned short int) ((((x) >> 8) & 0xffu) | \
	(((x) & 0xffu) << 8)))

/* Basic types */
#define	BYTE	uint8_t
#define	U16	uint16_t
#define	U32	uint32_t
#define	S32	int32_t
#define	U64	uint64_t

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack(1)
#endif

typedef struct _U16_S {
	U16 v;
} U16_S;
typedef struct _U32_S {
	U32 v;
} U32_S;
typedef struct _U64_S {
	U64 v;
} U64_S;

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack()
#endif

#define	A64(x) (((U64_S *)(x))->v)
#define	A32(x) (((U32_S *)(x))->v)
#define	A16(x) (((U16_S *)(x))->v)

/*
 * Constants
 */
#define	MINMATCH 4

#define	HASH_LOG COMPRESSIONLEVEL
#define	HASHTABLESIZE (1 << HASH_LOG)
#define	HASH_MASK (HASHTABLESIZE - 1)

#define	SKIPSTRENGTH (NOTCOMPRESSIBLE_CONFIRMATION > 2 ? \
	NOTCOMPRESSIBLE_CONFIRMATION : 2)

#define	COPYLENGTH 8
#define	LASTLITERALS 5
#define	MFLIMIT (COPYLENGTH + MINMATCH)
#define	MINLENGTH (MFLIMIT + 1)

#define	MAXD_LOG 16
#define	MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define	ML_BITS 4
#define	ML_MASK ((1U<<ML_BITS)-1)
#define	RUN_BITS (8-ML_BITS)
#define	RUN_MASK ((1U<<RUN_BITS)-1)


/*
 * Architecture-specific macros
 */
#if LZ4_ARCH64
#define	STEPSIZE 8
#define	UARCH U64
#define	AARCH A64
#define	LZ4_COPYSTEP(s, d)	A64(d) = A64(s); d += 8; s += 8;
#define	LZ4_COPYPACKET(s, d)	LZ4_COPYSTEP(s, d)
#define	LZ4_SECURECOPY(s, d, e)	if (d < e) LZ4_WILDCOPY(s, d, e)
#define	HTYPE U32
#define	INITBASE(base)		const BYTE* const base = ip
#else /* !LZ4_ARCH64 */
#define	STEPSIZE 4
#define	UARCH U32
#define	AARCH A32
#define	LZ4_COPYSTEP(s, d)	A32(d) = A32(s); d += 4; s += 4;
#define	LZ4_COPYPACKET(s, d)	LZ4_COPYSTEP(s, d); LZ4_COPYSTEP(s, d);
#define	LZ4_SECURECOPY		LZ4_WILDCOPY
#define	HTYPE const BYTE *
#define	INITBASE(base)		const int base = 0
#endif /* !LZ4_ARCH64 */

#if (defined(LZ4_BIG_ENDIAN) && !defined(BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE))
#define	LZ4_READ_LITTLEENDIAN_16(d, s, p) \
	{ U16 v = A16(p); v = lz4_bswap16(v); d = (s) - v; }
#define	LZ4_WRITE_LITTLEENDIAN_16(p, i) \
	{ U16 v = (U16)(i); v = lz4_bswap16(v); A16(p) = v; p += 2; }
#else
#define	LZ4_READ_LITTLEENDIAN_16(d, s, p) { d = (s) - A16(p); }
#define	LZ4_WRITE_LITTLEENDIAN_16(p, v)  { A16(p) = v; p += 2; }
#endif


/* Local structures */
struct refTables {
	HTYPE hashTable[HASHTABLESIZE];
};


/* Macros */
#define	LZ4_HASH_FUNCTION(i) (((i) * 2654435761U) >> ((MINMATCH * 8) - \
	HASH_LOG))
#define	LZ4_HASH_VALUE(p) LZ4_HASH_FUNCTION(A32(p))
#define	LZ4_WILDCOPY(s, d, e) do { LZ4_COPYPACKET(s, d) } while (d < e);
#define	LZ4_BLINDCOPY(s, d, l) { BYTE* e = (d) + l; LZ4_WILDCOPY(s, d, e); \
	d = e; }

/* Note : this function is valid only if isize < LZ4_64KLIMIT */
#define	LZ4_64KLIMIT ((1 << 16) + (MFLIMIT - 1))
#define	HASHLOG64K (HASH_LOG + 1)
#define	HASH64KTABLESIZE (1U << HASHLOG64K)
#define	LZ4_HASH64K_FUNCTION(i)	(((i) * 2654435761U) >> ((MINMATCH*8) - \
	HASHLOG64K))
#define	LZ4_HASH64K_VALUE(p)	LZ4_HASH64K_FUNCTION(A32(p))


/* Decompression functions */

/*
 * Note: The decoding functions real_LZ4_uncompress() and
 *	LZ4_uncompress_unknownOutputSize() are safe against "buffer overflow"
 *	attack type. They will never write nor read outside of the provided
 *	output buffers. LZ4_uncompress_unknownOutputSize() also insures that
 *	it will never read outside of the input buffer. A corrupted input
 *	will produce an error result, a negative int, indicating the position
 *	of the error within input stream.
 *
 * Note[2]: real_LZ4_uncompress(), referred to above, is not used in ZFS so
 *	its code is not present here.
 */

static int
LZ4_uncompress_unknownOutputSize(const char *source, char *dest, int isize,
    int maxOutputSize)
{
	/* Local Variables */
	const BYTE *restrict ip = (const BYTE *) source;
	const BYTE *const iend = ip + isize;
	const BYTE *ref;

	BYTE *op = (BYTE *) dest;
	BYTE *const oend = op + maxOutputSize;
	BYTE *cpy;

	size_t dec32table[] = {0, 3, 2, 3, 0, 0, 0, 0};
#if LZ4_ARCH64
	size_t dec64table[] = {0, 0, 0, (size_t)-1, 0, 1, 2, 3};
#endif

	/* Main Loop */
	while (ip < iend) {
		unsigned token;
		size_t length;

		/* get runlength */
		token = *ip++;
		if ((length = (token >> ML_BITS)) == RUN_MASK) {
			int s = 255;
			while ((ip < iend) && (s == 255)) {
				s = *ip++;
				length += s;
			}
		}
		/* copy literals */
		cpy = op + length;
		/* CORNER-CASE: cpy might overflow. */
		if (cpy < op)
			goto _output_error;	/* cpy was overflowed, bail! */
		if ((cpy > oend - COPYLENGTH) ||
		    (ip + length > iend - COPYLENGTH)) {
			if (cpy > oend)
				/* Error: writes beyond output buffer */
				goto _output_error;
			if (ip + length != iend)
				/*
				 * Error: LZ4 format requires to consume all
				 * input at this stage
				 */
				goto _output_error;
			(void) memcpy(op, ip, length);
			op += length;
			/* Necessarily EOF, due to parsing restrictions */
			break;
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/* get offset */
		LZ4_READ_LITTLEENDIAN_16(ref, cpy, ip);
		ip += 2;
		if (ref < (BYTE * const) dest)
			/*
			 * Error: offset creates reference outside of
			 * destination buffer
			 */
			goto _output_error;

		/* get matchlength */
		if ((length = (token & ML_MASK)) == ML_MASK) {
			while (ip < iend) {
				int s = *ip++;
				length += s;
				if (s == 255)
					continue;
				break;
			}
		}
		/* copy repeated sequence */
		if (unlikely(op - ref < STEPSIZE)) {
#if LZ4_ARCH64
			size_t dec64 = dec64table[op-ref];
#else
			const int dec64 = 0;
#endif
			op[0] = ref[0];
			op[1] = ref[1];
			op[2] = ref[2];
			op[3] = ref[3];
			op += 4;
			ref += 4;
			ref -= dec32table[op-ref];
			A32(op) = A32(ref);
			op += STEPSIZE - 4;
			ref -= dec64;
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE - 4);
		if (cpy > oend - COPYLENGTH) {
			if (cpy > oend)
				/*
				 * Error: request to write outside of
				 * destination buffer
				 */
				goto _output_error;
			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			if (op == oend)
				/*
				 * Check EOF (should never happen, since
				 * last 5 bytes are supposed to be literals)
				 */
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy;	/* correction */
	}

	/* end of decoding */
	return (int)(((char *)op) - dest);

	/* write overflow error detected */
	_output_error:
	return (int)(-(((char *)ip) - source));
}


/* ************************************************************************* */


#define MAX(a, b)               ((a) > (b) ? (a) : (b))

typedef int (*compressor)(const char *source, char *dest, int isize);

static inline void store_le32(char *c, uint32_t x) {
    c[0] = x & 0xff;
    c[1] = (x >> 8) & 0xff;
    c[2] = (x >> 16) & 0xff;
    c[3] = (x >> 24) & 0xff;
}

static inline uint32_t load_le32(const char *c) {
    const uint8_t *d = (const uint8_t *)c;
    return d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
}

static const int hdr_size = sizeof(uint32_t);

static PyObject *py_lz4_uncompress(PyObject *self, PyObject *args) {
    PyObject *result;
    const char *source;
    int source_size;
    uint32_t dest_size;

    if (!PyArg_ParseTuple(args, "s#", &source, &source_size)) {
        return NULL;
    }

    if (source_size < hdr_size) {
        PyErr_SetString(PyExc_ValueError, "input too short");
        return NULL;
    }
    dest_size = load_le32(source);
    if (dest_size > INT_MAX) {
        PyErr_Format(PyExc_ValueError, "invalid size in header: 0x%x", dest_size);
        return NULL;
    }
    result = PyBytes_FromStringAndSize(NULL, dest_size);
    if (result != NULL && dest_size > 0) {
        char *dest = PyBytes_AS_STRING(result);
        int osize = LZ4_decompress_safe(source + hdr_size, dest, source_size - hdr_size, dest_size);
        if (osize < 0) {
            PyErr_Format(PyExc_ValueError, "corrupt input at byte %d", -osize);
            Py_CLEAR(result);
        }
    }

    return result;
}

static PyMethodDef Lz4Methods[] = {
    {"decompress",  py_lz4_uncompress, METH_VARARGS, UNCOMPRESS_DOCSTRING},
    {NULL, NULL, 0, NULL}
};

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif

#if PY_MAJOR_VERSION >= 3

static int myextension_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int myextension_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}


static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "lz4zfs",
        NULL,
        sizeof(struct module_state),
        Lz4Methods,
        NULL,
        myextension_traverse,
        myextension_clear,
        NULL
};

#define INITERROR return NULL
PyObject *PyInit_lz4zfs(void)

#else
#define INITERROR return
void initlz4zfs(void)

#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("lz4zfs", Lz4Methods);
#endif
    struct module_state *st = NULL;

    if (module == NULL) {
        INITERROR;
    }
    st = GETSTATE(module);

    st->error = PyErr_NewException("lz4.Error", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(module);
        INITERROR;
    }

    PyModule_AddStringConstant(module, "VERSION", VERSION);
    PyModule_AddStringConstant(module, "LZ4_VERSION", LZ4_VERSION);

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
