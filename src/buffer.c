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
  last mod: $Id: buffer.c,v 1.1.2.15 2003/07/18 04:45:21 xiphmont Exp $

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

#ifdef OGGBUFFER_DEBUG
    fprintf(stderr,"\nZero-copy pool %p lazy destroy: %d buffers outstanding.\n",
	    bs,bs->outstanding);
    if(!bs->outstanding)
      fprintf(stderr,"Finishing memory cleanup of %p.\n",bs);
#endif

    bt=bs->unused_buffers;
    rt=bs->unused_references;

    if(!bs->outstanding){
      ogg_mutex_unlock(&bs->mutex);
      ogg_mutex_clear(&bs->mutex);
      _ogg_free(bs);
      return;
    }else
      ogg_mutex_unlock(&bs->mutex);

    while(bt){
      ogg_buffer *b=bt;
      bt=b->ptr.next;
      if(b->data)_ogg_free(b->data);
      _ogg_free(b);
    }
    bs->unused_buffers=0;
    while(rt){
      ogg_reference *r=rt;
      rt=r->next;
      _ogg_free(r);
    }
    bs->unused_references=0;
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
    ob->data=_ogg_malloc(bytes<OGGPACK_MINCHUNKSIZE?
			 OGGPACK_MINCHUNKSIZE:bytes);
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
#ifdef OGGBUFFER_DEBUG
  or->used=1;
#endif
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
ogg_reference *ogg_buffer_sub(ogg_reference *or,long begin,long length){
  ogg_reference *ret=0,*head=0;

  /* walk past any preceeding fragments we don't want */
  while(or && begin>=or->length){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    begin-=or->length;
    or=or->next;
  }

  /* duplicate the reference chain; increment refcounts */
  while(or && length){
    ogg_reference *temp=_fetch_ref(or->buffer->ptr.owner);
    if(head)
      head->next=temp;
    else
      ret=temp;
    head=temp;

#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif

    head->buffer=or->buffer;
    
    head->begin=or->begin+begin;
    head->length=length;
    if(head->length>or->length-begin)
      head->length=or->length-begin;
    
    begin=0;
    length-=head->length;
    or=or->next;
  }

  ogg_buffer_mark(ret);
  return ret;
}

ogg_reference *ogg_buffer_dup(ogg_reference *or){
  ogg_reference *ret=0,*head=0;

  /* duplicate the reference chain; increment refcounts */
  while(or){
    ogg_reference *temp=_fetch_ref(or->buffer->ptr.owner);
    if(head)
      head->next=temp;
    else
      ret=temp;
    head=temp;

#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif

    head->buffer=or->buffer;
    head->begin=or->begin;
    head->length=or->length;
    or=or->next;
  }

  ogg_buffer_mark(ret);
  return ret;
}

static void _ogg_buffer_mark_one(ogg_reference *or){
  ogg_buffer_state *bs=or->buffer->ptr.owner;
  ogg_mutex_lock(&bs->mutex); /* lock now in case someone is mixing
				 pools */
  
#ifdef OGGBUFFER_DEBUG
  if(or->buffer->refcount==0){
    fprintf(stderr,"WARNING: marking buffer fragment with refcount of zero!\n");
    exit(1);
  }
  if(or->used==0){
    fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
    exit(1);
  }
#endif
  
  or->buffer->refcount++;
  ogg_mutex_unlock(&bs->mutex);
}

/* split a reference into two references; 'return' is a reference to
   the buffer preceeding pos and 'head'/'tail' are the buffer past the
   split.  If pos is at or past the end of the passed in segment,
   'head/tail' are NULL */
ogg_reference *ogg_buffer_split(ogg_reference **tail,
				ogg_reference **head,long pos){

  /* walk past any preceeding fragments to one of:
     a) the exact boundary that seps two fragments
     b) the fragment that needs split somewhere in the middle */
  ogg_reference *ret=*tail;
  ogg_reference *or=*tail;

  while(or && pos>or->length){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    pos-=or->length;
    or=or->next;
  }

  if(!or || pos==0){

    return 0;
    
  }else{
    
    if(pos>=or->length){
      /* exact split, or off the end? */
      if(or->next){
	
	/* a split */
	*tail=or->next;
	or->next=0;
	
      }else{
	
	/* off or at the end */
	*tail=*head=0;
	
      }
    }else{
      
      /* split within a fragment */
      long lengthA=pos;
      long beginB=or->begin+pos;
      long lengthB=or->length-pos;
      
      /* make a new reference to tail the second piece */
      *tail=_fetch_ref(or->buffer->ptr.owner);
      
      (*tail)->buffer=or->buffer;
      (*tail)->begin=beginB;
      (*tail)->length=lengthB;
      (*tail)->next=or->next;
      _ogg_buffer_mark_one(*tail);
      if(head && or==*head)*head=*tail;    
      
      /* update the first piece */
      or->next=0;
      or->length=lengthA;
      
    }
  }
  return ret;
}

/* add a new fragment link to the end of a chain; return ptr to the new link */
ogg_reference *ogg_buffer_extend(ogg_reference *or,long bytes){
  if(or){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    while(or->next){
      or=or->next;
#ifdef OGGBUFFER_DEBUG
      if(or->used==0){
	fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
	exit(1);
      }
#endif
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
  if(ob->refcount==0){
    ogg_mutex_unlock(&bs->mutex);
    fprintf(stderr,"WARNING: releasing buffer fragment with refcount of zero!\n");
    exit(1);
  }
  if(or->used==0){
    ogg_mutex_unlock(&bs->mutex);
    fprintf(stderr,"WARNING: releasing previously released reference!\n");
    exit(1);
  }
  or->used=0;
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
  ogg_mutex_unlock(&bs->mutex);

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
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    or->begin+=pos;
    or->length-=pos;
  }
  return or;
}

void ogg_buffer_posttruncate(ogg_reference *or,long pos){
  /* walk to the point where we want to begin truncate */
  while(or && pos>or->length){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
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
  if(!or)return NULL;
  while(or->next){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    or=or->next;
  }
#ifdef OGGBUFFER_DEBUG
  if(or->used==0){
    fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
    exit(1);
  }
#endif
  return(or);
}

/* *head is appended to the front end (head) of *tail; both continue to
   be valid pointers, with *tail at the tail and *head at the head */
ogg_reference *ogg_buffer_cat(ogg_reference *tail, ogg_reference *head){
  if(!tail)return head;

  while(tail->next){
#ifdef OGGBUFFER_DEBUG
    if(tail->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    tail=tail->next;
  }
#ifdef OGGBUFFER_DEBUG
  if(tail->used==0){
    fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
    exit(1);
  }
#endif
  tail->next=head;
  return ogg_buffer_walk(head);
}

long ogg_buffer_length(ogg_reference *or){
  int count=0;
  while(or){
#ifdef OGGBUFFER_DEBUG
    if(or->used==0){
      fprintf(stderr,"\nERROR: Using reference marked as usused.\n");
      exit(1);
    }
#endif
    count+=or->length;
    or=or->next;
  }
  return count;
}

void ogg_buffer_outstanding(ogg_buffer_state *bs){
#ifdef OGGBUFFER_DEBUG
  fprintf(stderr,"Zero-copy pool %p: %d buffers outstanding.\n",
	  bs,bs->outstanding);
#endif
}
