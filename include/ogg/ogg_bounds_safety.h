/* ogg_bounds_safety.h - portability macros for optional -fbounds-safety
 *
 * Copyright (c) 2026 Jeff Bindel
 *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.
 *
 * When OGG_SUPPORT_FBOUNDS_SAFETY is defined (typically via
 * -DOGG_SUPPORT_FBOUNDS_SAFETY and a Clang toolchain that implements
 * -fbounds-safety), these macros expand to Clang bounds annotations.
 * Otherwise they expand to nothing so default builds are unchanged.
 *
 * Pattern matches libwebp / libpng / giflib inert-macro
 * -fbounds-safety adoption: annotations are inert unless explicitly enabled.
 */

#ifndef OGG_BOUNDS_SAFETY_H
#define OGG_BOUNDS_SAFETY_H

#ifdef OGG_SUPPORT_FBOUNDS_SAFETY

#  include <ptrcheck.h>
/* Non-ABI-breaking counted-by annotations for struct pointer members.
 * Prefer OGG_COUNTED_BY_OR_NULL for pointers that may be NULL while the
 * companion size field is zero (ogg_stream_state.body_data pattern).
 */
#  define OGG_COUNTED_BY(n) __counted_by(n)
#  define OGG_COUNTED_BY_OR_NULL(n) __counted_by_or_null(n)

#else /* !OGG_SUPPORT_FBOUNDS_SAFETY */

#  define OGG_COUNTED_BY(n)
#  define OGG_COUNTED_BY_OR_NULL(n)

#endif /* OGG_SUPPORT_FBOUNDS_SAFETY */

#endif /* OGG_BOUNDS_SAFETY_H */
