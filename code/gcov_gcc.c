/**********************************************************************/
/** @addtogroup embedded_gcov
 * @{
 * @file
 * @version $Id: $
 *
 * @author 2021-08-31 kjpeters  Working and cleaned up version.
 *
 * @note Based on GCOV-related code of the Linux kernel,
 * as described online by Thanassis Tsiodras (April 2016)
 * https://www.thanassis.space/coverage.html
 * and by Alexander Tarasikov
 * http://allsoftwaresucks.blogspot.com/2015/05/gcov-is-amazing-yet-undocumented.html
 * with additional investigation, updating, cleanup, and portability
 * by Ken Peters.
 *
 * @brief Private source file for embedded gcov.
 *
 **********************************************************************/
/*
 *  This code provides functions to handle gcc's profiling data format
 *  introduced with gcc 4.7.
 *
 *  This file is based heavily on gcc_3_4.c file.
 *
 *  For a better understanding, refer to gcc source:
 *  gcc/gcov-io.h
 *  libgcc/libgcov.c
 *
 *  Uses gcc-internal data definitions.
 */

#include "gcov_gcc.h"

#ifdef GCOV_OPT_RESET_WATCHDOG
/* In an embedded system, you might want to reset any watchdog timer below, */
/* depending on your timeout versus gcov tree size */
/* If so, include any needed header files. */
#include "all.h"
#include "defs.h"
#endif // GCOV_OPT_RESET_WATCHDOG


/**
 * struct gcov_ctr_info - information about counters for a single function
 * @num: number of counter values for this type
 * @values: array of counter values for this type
 *
 * This data is generated by gcc during compilation and doesn't change
 * at run-time with the exception of the values array.
 */
/* Compare to libgcc/libgcov.h */
struct gcov_ctr_info {
	gcov_unsigned_t num;
	gcov_type *values;
};

/**
 * struct gcov_fn_info - profiling meta data per function
 * @key: comdat key
 * @ident: unique ident of function
 * @lineno_checksum: function lineo_checksum
 * @cfg_checksum: function cfg checksum
 * @ctrs: instrumented counters
 *
 * This data is generated by gcc during compilation and doesn't change
 * at run-time.
 *
 * Information about a single function.  This uses the trailing array
 * idiom. The number of counters is determined from the merge pointer
 * array in gcov_info.  The key is used to detect which of a set of
 * comdat functions was selected -- it points to the gcov_info object
 * of the object file containing the selected comdat function.
 */
/* Compare to libgcc/libgcov.h */
struct gcov_fn_info {
	const struct gcov_info *key;
	gcov_unsigned_t ident;
	gcov_unsigned_t lineno_checksum;
	gcov_unsigned_t cfg_checksum;
	struct gcov_ctr_info ctrs[1];
};

/* Type of function used to merge counters.  */
/* Compare to libgcc/libgcov.h */
typedef void (*gcov_merge_fn) (gcov_type *, gcov_unsigned_t);

/**
 * struct gcov_info - profiling data per object file
 * @version: gcov version magic indicating the gcc version used for compilation
 * @next: list head for a singly-linked list
 * @stamp: uniquifying time stamp
 * @filename: name of the associated gcov data file
 * @merge: merge functions (null for unused counter type)
 * @n_functions: number of instrumented functions
 * @functions: pointer to pointers to function information
 *
 * This data is generated by gcc during compilation and doesn't change
 * at run-time with the exception of the next pointer.
 */
/* Compare to libgcc/libgcov.h */
struct gcov_info {
	gcov_unsigned_t version;
	struct gcov_info *next;
	gcov_unsigned_t stamp;
    gcov_unsigned_t checksum;
	const char *filename;
	gcov_merge_fn merge[GCOV_COUNTERS];
	unsigned n_functions;
	struct gcov_fn_info **functions;
};

/**
 * gcov_info_filename - return info filename
 * @info: profiling data set
 *
 * Need this to access opaque gcov_info to get filename in public code.
 */
const char *gcov_info_filename(struct gcov_info *info)
{
	return info->filename;
}

/* See gcc/gcov-io.h for description of number formats */

/**
 * store_gcov_unsigned - store 32 bit number in gcov format to buffer
 * @buffer: target buffer or NULL
 * @off: offset into the buffer
 * @v: value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. Returns the number of bytes stored. If @buffer is %NULL, doesn't
 * store anything.
 */
/* Slightly like gcc/gcov-io.c function gcov_write_unsigned() (1-word item) */
/* Need buffer to be 32-bit-aligned for type-safe internal usage */
static size_t store_gcov_unsigned(gcov_unsigned_t *buffer, size_t off, gcov_unsigned_t v)
{
	gcov_unsigned_t *data;

	if (buffer) {
		data = buffer + off;
		*data = v;
	}

	/* return count of buffer data type units */
	return sizeof(*data)/sizeof(*buffer);
}

/**
 * store_gcov_tag_length - 32 bit tag and 32 bit length in gcov format to buffer
 * @buffer: target buffer or NULL
 * @off: offset into the buffer
 * @tag: tag value to be stored
 * @length: length value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. 64 bit numbers are stored as two 32 bit numbers, the low part
 * first. Returns the number of bytes stored. If @buffer is %NULL, doesn't store
 * anything.
 */
/* Slightly like gcc/gcov-io.c function gcov_write_tag_length() (1-word tag and 1-word length) */
/* or gcov_write_tag() (1-word tag and implied 1-word "length = 0") */
/* Need buffer to be 32-bit-aligned for type-safe internal usage */
static size_t store_gcov_tag_length(gcov_unsigned_t *buffer, size_t off, gcov_unsigned_t tag, gcov_unsigned_t length)
{
	gcov_unsigned_t *data;

	if (buffer) {
		data = buffer + off;

		data[0] = tag;
		data[1] = length;
	}

	/* return count of buffer data type units */
	return sizeof(*data)/sizeof(*buffer) * 2;
}

/**
 * store_gcov_counter - store 64 bit number in gcov format to buffer
 * @buffer: target buffer or NULL
 * @off: offset into the buffer
 * @v: value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. 64 bit numbers are stored as two 32 bit numbers, the low part
 * first. Returns the number of bytes stored. If @buffer is %NULL, doesn't store
 * anything.
 */
/* Slightly like gcc/gcov-io.c function gcov_write_counter() (2-word item) */
/* Need buffer to be 32-bit-aligned for type-safe internal usage */
static size_t store_gcov_counter(gcov_unsigned_t *buffer, size_t off, gcov_type v)
{
	gcov_unsigned_t *data;

	if (buffer) {
		data = buffer + off;

		data[0] = (v & 0xffffffffUL);
		data[1] = (v >> 32);
	}

	/* return count of buffer data type units */
	return sizeof(*data)/sizeof(*buffer) * 2;
}

/**
 * gcov_convert_to_gcda - convert profiling data set to gcda file format
 * @buffer: the buffer to store file data or %NULL if no data should be stored
 * @info: profiling data set to be converted
 *
 * Returns the number of bytes that were/would have been stored into the buffer.
 */
/* Our own creation, but compare to libgcc/libgcov-driver.c function write_one_data() */
/* Need buffer to be 32-bit-aligned for type-safe internal usage */
size_t gcov_convert_to_gcda(gcov_unsigned_t *buffer, struct gcov_info *gi_ptr)
{
	const struct gcov_fn_info *fi_ptr;
	const struct gcov_ctr_info *ci_ptr;
	unsigned int fi_idx;
	unsigned int ct_idx;
	unsigned int cv_idx;
	size_t pos = 0; /* offset in buffer, in buffer data type units */

	/* File header. */
	pos += store_gcov_tag_length(buffer, pos, GCOV_DATA_MAGIC, gi_ptr->version);
	pos += store_gcov_unsigned(buffer, pos, gi_ptr->stamp);
	pos += store_gcov_unsigned(buffer, pos, gi_ptr->checksum);

	/* Write execution counts for each function.  */
	for (fi_idx = 0; fi_idx < gi_ptr->n_functions; fi_idx++) {
		fi_ptr = gi_ptr->functions[fi_idx];

#ifdef GCOV_OPT_RESET_WATCHDOG
		/* In an embedded system, you might want to reset any watchdog timer here, */
		/* depending on your timeout versus gcov tree size */
		SP_WDG = WATCHDOG_RESET;
#endif // GCOV_OPT_RESET_WATCHDOG

		/* Function record. */
		pos += store_gcov_tag_length(buffer, pos, GCOV_TAG_FUNCTION, GCOV_TAG_FUNCTION_LENGTH);

		pos += store_gcov_unsigned(buffer, pos, fi_ptr->ident);
		pos += store_gcov_unsigned(buffer, pos, fi_ptr->lineno_checksum);
		pos += store_gcov_unsigned(buffer, pos, fi_ptr->cfg_checksum);

		ci_ptr = fi_ptr->ctrs;

		for (ct_idx = 0; ct_idx < GCOV_COUNTERS; ct_idx++) {
			if (!gi_ptr->merge[ct_idx]) {
				/* Unused counter */
				continue;
			}

			/* Counter record. */
			pos += store_gcov_tag_length(buffer, pos,
					      GCOV_TAG_FOR_COUNTER(ct_idx),
					      GCOV_TAG_COUNTER_LENGTH(ci_ptr->num));

			for (cv_idx = 0; cv_idx < ci_ptr->num; cv_idx++) {
				pos += store_gcov_counter(buffer, pos,
						      ci_ptr->values[cv_idx]);
			}
			ci_ptr++;
		}
	}

	/* return count of bytes (convert from count of buffer data type units) */
	return pos * sizeof(*buffer);
}

/**
 * gcov_clear_counters - set profiling counters to zero
 * @info: profiling data set to be cleared
 *
 */
/* Our own creation, but compare to libgcc/libgcov-driver.c function write_one_data() */
void gcov_clear_counters(struct gcov_info *gi_ptr)
{
	const struct gcov_fn_info *fi_ptr;
	const struct gcov_ctr_info *ci_ptr;
	unsigned int fi_idx;
	unsigned int ct_idx;
	unsigned int cv_idx;

	/* Clear execution counts for each function.  */
	for (fi_idx = 0; fi_idx < gi_ptr->n_functions; fi_idx++) {
		fi_ptr = gi_ptr->functions[fi_idx];

		ci_ptr = fi_ptr->ctrs;

		for (ct_idx = 0; ct_idx < GCOV_COUNTERS; ct_idx++) {
			if (!gi_ptr->merge[ct_idx]) {
				/* Unused counter */
				continue;
			}

			/* Counter record. */
			for (cv_idx = 0; cv_idx < ci_ptr->num; cv_idx++) {
			      ci_ptr->values[cv_idx] = 0;
			}
			ci_ptr++;
		}
	}

	return;
}

/** @}
 */
/*
 * embedded-gcov gcov_gcc.c gcov internals interface code
 *
 * Copyright (c) 2021 California Institute of Technology (“Caltech”).
 * U.S. Government sponsorship acknowledged.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *    Redistributions of source code must retain the above copyright notice,
 *        this list of conditions and the following disclaimer.
 *    Redistributions in binary form must reproduce the above copyright notice,
 *        this list of conditions and the following disclaimer in the documentation
 *        and/or other materials provided with the distribution.
 *    Neither the name of Caltech nor its operating division, the Jet Propulsion Laboratory,
 *        nor the names of its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
