/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2002             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: #ifdef jail for basic thread mutexing
 last mod: $Id: mutex.c,v 1.1.2.1 2003/03/06 23:12:27 xiphmont Exp $

 ********************************************************************/

/* the thread mutexing here is for internal Ogg use only; the
   zero-copy code in libogg2 centralizes buffer management in one
   place where libogg1 spread it across abstraction layers. 

   Mutexing code isn't requird for a platform, it's only required to
   use threads with libogg and oggfile */

#include <ogg/ogg.h>

#ifdef USE_POSIX_THREADS
#include <pthread.h>

extern void ogg_mutex_init(ogg_mutex_t *mutex){


}



#else
#ifdef USE_NO_THREADS





#else

#error "this platform has no threading primitive wrappers in ogg/mutex.c"

#endif
