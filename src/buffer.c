/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2003             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

  function: centralized fragment buffer management
  last mod: $Id: buffer.c,v 1.1.2.9 2003/03/23 23:40:58 xiphmont Exp $

 ********************************************************************/

#ifdef OGGBUFFER_DEBUG
#include <stdio.h>
#endif

#include <stdlib.h>
#include "ogginternal.h"

/* basic, centralized Ogg memory management based on linked lists of
   references to refcounted memory buffers.  References and buffers
   are both recycled.  Buffers are passed around and consumed in
   reference form. */

/* management is here; actual production and consumption of data is
   found in the rest of the libogg code */

ogg_buffer_state *ogg_buffer_create(void){
  ogg_buffer_state *bs=_ogg_calloc(1,sizeof(*bs));
  ogg_mutex_init(&bs->mutex);
  return bs;
}

/* destruction is 'lazy'; there may be memory references outstanding,
   and yanking the buffer state out from underneath would be
   antisocial.  Dealloc what is currently unused and have
   _release_one watch for the stragglers to come in.  When they do,
   finish destruction. */

/* call the helper while holding lock */
static void _ogg_buffer_destroy(ogg_buffer_state *bs){
  ogg_buffer *bt;
  ogg_reference *rt;

  if(bs->shutdown){
    bt=bs->unused_buffers;
    rt=bs->unused_references;

    if(!bs->outstanding){
      ogg_mutex_unlock(&bs->mutex);
      ogg_mutex_clear(&bs->mutex);
      _ogg_free(bs);
    }else
      ogg_mutex_unlock(&bs->mutex);

    while(bt){
      ogg_buffer *b=bt;
      bt=b->ptr.next;
      if(b->data)_ogg_free(b->data);
      _ogg_free(b);
    }
    while(rt){
      ogg_reference *r=rt;
      rt=r->next;
      _ogg_free(r);
    }
  }
}

void ogg_buffer_destroy(ogg_buffer_state *bs){
  ogg_mutex_lock(&bs->mutex);
  bs->shutdown=1;
  _ogg_buffer_destroy(bs);
}

static ogg_buffer *_fetch_buffer(ogg_buffer_state *bs,long bytes){
  ogg_buffer    *ob;
  ogg_mutex_lock(&bs->mutex);
  bs->outstanding++;

  /* do we have an unused buffer sitting in the pool? */
  if(bs->unused_buffers){
    ob=bs->unused_buffers;
    bs->unused_buffers=ob->ptr.next;
    ogg_mutex_unlock(&bs->mutex);

    /* if the unused buffer is too small, grow it */
    if(ob->size<bytes){
      ob->data=_ogg_realloc(ob->data,bytes);
      ob->size=bytes;
    }
  }else{
    /* allocate a new buffer */
    ogg_mutex_unlock(&bs->mutex);
    ob=_ogg_malloc(sizeof(*ob));
    ob->data=_ogg_malloc(bytes);
    ob->size=bytes;
  }

  ob->refcount=1;
  ob->ptr.owner=bs;
  return ob;
}

static ogg_reference *_fetch_ref(ogg_buffer_state *bs){
  ogg_reference *or;
  ogg_mutex_lock(&bs->mutex);
  bs->outstanding++;

  /* do we have an unused reference sitting in the pool? */
  if(bs->unused_references){
    or=bs->unused_references;
    bs->unused_references=or->next;
    ogg_mutex_unlock(&bs->mutex);
  }else{
    /* allocate a new reference */
    ogg_mutex_unlock(&bs->mutex);
    or=_ogg_malloc(sizeof(*or));
  }

  or->begin=0;
  or->length=0;
  or->next=0;

  return or;
}

/* fetch a reference pointing to a fresh, initially continguous buffer
   of at least [bytes] length */
ogg_reference *ogg_buffer_alloc(ogg_buffer_state *bs,long bytes){
  ogg_buffer    *ob=_fetch_buffer(bs,bytes);
  ogg_reference *or=_fetch_ref(bs);
  or->buffer=ob;
  return or;
}

/* enlarge the data buffer in the current link */
void ogg_buffer_realloc(ogg_reference *or,long bytes){
  ogg_buffer    *ob=or->buffer;
  
  /* if the unused buffer is too small, grow it */
  if(ob->size<bytes){
    ob->data=_ogg_realloc(ob->data,bytes);
    ob->size=bytes;
  }
}

/* duplicate a reference (pointing to the same actual buffer memory)
   and increment buffer refcount.  If the desired segment begins out
   of range, NULL is returned; if the desired segment is simply zero
   length, a zero length ref is returned.  Partial range overlap
   returns the overlap of the ranges */
ogg_reference *ogg_buffer_dup(ogg_reference *or,long begin,long length){
  ogg_reference *ret=0,*head=0;

  /* walk past any preceeding fragments we don't want */
  while(or && begin>=or->length){
    begin-=or->length;
    or=or->next;
  }

  /* duplicate the reference chain; increment refcounts */
  while(or && length){
    ogg_reference *temp=_fetch_ref(or->buffer->ptr.owner);
    if(head)head->next=temp;
    head=temp;
    if(!ret)ret=head;

    head->buffer=or->buffer;
    
    head->begin=or->begin+begin;
    head->length=length;
    if(head->begin+head->length>or->begin+or->length)
      head->length=or->begin+or->length-head->begin;

    begin=0;
    length-=head->length;
  }

  ogg_buffer_mark(ret);
  return ret;
}

static void _ogg_buffer_mark_one(ogg_reference *or){
  ogg_buffer_state *bs=or->buffer->ptr.owner;
  ogg_mutex_lock(&bs->mutex); /* lock now in case someone is mixing
				 pools */
  
#ifdef OGGBUFFER_DEBUG
  if(or->buffer->refcount==0)
    fprintf(stderr,"WARNING: marking buffer fragment with refcount of zero!\n");
#endif
  
  or->buffer->refcount++;
  ogg_mutex_unlock(&bs->mutex);
}

/* split a reference into two references; on return the passed in
   pointer points to the first segment (pos of zero disallowed).
   pointer to the beginning of the secrond reference is returned.  If
   pos is at or past the end of the passed in segment, returns NULL */
ogg_reference *ogg_buffer_split(ogg_reference *or,long pos){

  /* walk past any preceeding fragments to one of:
     a) the exact boundary that seps two fragments
     b) the fragment that needs split somewhere in the middle */
  
  while(or && pos>or->length){
    pos-=or->length;
    or=or->next;
  }

  if(pos>=or->length){
    /* exact split, or off the end */
    if(or->next){

      /* a split */
      ogg_reference *ret=or->next;
      or->next=0;
      return ret;

    }else{

      /* off or at the end */
      return NULL;

    }
  }else{

    /* split within a fragment */
    long lengthA=pos;
    long beginB=or->begin+pos;
    long lengthB=or->length-pos;

    /* make a new reference to head the second piece */
    ogg_reference *ret=_fetch_ref(or->buffer->ptr.owner);

    ret->buffer=or->buffer;
    ret->begin=beginB;
    ret->length=lengthB;
    ret->next=or->next;
    _ogg_buffer_mark_one(ret);

    /* update the first piece */
    or->next=0;
    or->length=lengthA;

    return ret;
  }
}

/* add a new fragment link to the end of a chain; return ptr to the new link */
ogg_reference *ogg_buffer_extend(ogg_reference *or,long bytes){
  if(or){
    while(or->next){
      or=or->next;
    }
    or->next=ogg_buffer_alloc(or->buffer->ptr.owner,bytes);
    return(or->next);
  }
  return 0;
}

/* increase the refcount of the buffers to which the reference points */
void ogg_buffer_mark(ogg_reference *or){
  while(or){
    _ogg_buffer_mark_one(or);
    or=or->next;
  }
}

void ogg_buffer_release_one(ogg_reference *or){
  ogg_buffer *ob=or->buffer;
  ogg_buffer_state *bs=ob->ptr.owner;
  ogg_mutex_lock(&bs->mutex);

#ifdef OGGBUFFER_DEBUG
  if(ob->refcount==0)
    fprintf(stderr,"WARNING: releasing buffer fragment with refcount of zero!\n");
#endif
  
  ob->refcount--;
  if(ob->refcount==0){
    bs->outstanding--; /* for the returned buffer */
    ob->ptr.next=bs->unused_buffers;
    bs->unused_buffers=ob;
  }
  
  bs->outstanding--; /* for the returned reference */
  or->next=bs->unused_references;
  bs->unused_references=or;

  _ogg_buffer_destroy(bs); /* lazy cleanup (if needed) */

}

/* release the references, decrease the refcounts of buffers to which
   they point, release any buffers with a refcount that drops to zero */
void ogg_buffer_release(ogg_reference *or){
  while(or){
    ogg_reference *next=or->next;
    ogg_buffer_release_one(or);
    or=next;
  }
}

ogg_reference *ogg_buffer_pretruncate(ogg_reference *or,long pos){
  /* release preceeding fragments we don't want */
  while(or && pos>=or->length){
    ogg_reference *next=or->next;
    pos-=or->length;
    ogg_buffer_release_one(or);
    or=next;
  }
  if (or) {
    or->begin+=pos;
    or->length-=pos;
  }
  return or;
}

void ogg_buffer_posttruncate(ogg_reference *or,long pos){
  /* walk to the point where we want to begin truncate */
  while(or && pos>or->length){
    pos-=or->length;
    or=or->next;
  }
  if(or){
    /* release or->next and beyond */
    ogg_buffer_release(or->next);
    or->next=0;
    /* update length fencepost */
    or->length=pos;
  }
}

ogg_reference *ogg_buffer_walk(ogg_reference *or){
  while(or->next)or=or->next;
  return(or);
}

/* *head is appended to the front end (head) of *tail; both continue to
   be valid pointers, with *tail at the tail and *head at the head */
ogg_reference *ogg_buffer_cat(ogg_reference *tail, ogg_reference *head){
  while(tail->next){
    tail=tail->next;
  }
  tail->next=head;
  return ogg_buffer_walk(head);
}

long ogg_buffer_length(ogg_reference *or){
  int count=0;
  while(or){
    count+=or->length;
    or=or->next;
  }
  return count;
}
