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

  function: pack variable sized words into an octet stream
  last mod: $Id: bitwise.c,v 1.14.2.14 2003/03/27 07:12:45 xiphmont Exp $

 ********************************************************************/

/* the 'oggpack_xxx functions are 'LSb' endian; if we write a word but
   read individual bits, then we'll read the lsb first */
/* the 'oggpackB_xxx functions are 'MSb' endian; if we write a word but
   read individual bits, then we'll read the msb first */

#include <string.h>
#include <stdlib.h>
#include "ogginternal.h"

static unsigned long mask[]=
{0x00000000,0x00000001,0x00000003,0x00000007,0x0000000f,
 0x0000001f,0x0000003f,0x0000007f,0x000000ff,0x000001ff,
 0x000003ff,0x000007ff,0x00000fff,0x00001fff,0x00003fff,
 0x00007fff,0x0000ffff,0x0001ffff,0x0003ffff,0x0007ffff,
 0x000fffff,0x001fffff,0x003fffff,0x007fffff,0x00ffffff,
 0x01ffffff,0x03ffffff,0x07ffffff,0x0fffffff,0x1fffffff,
 0x3fffffff,0x7fffffff,0xffffffff };

void oggpack_writeinit(oggpack_buffer *b,ogg_buffer_state *bs){
  memset(b,0,sizeof(*b));
  b->owner=bs;
}

void oggpackB_writeinit(oggpack_buffer *b,ogg_buffer_state *bs){
  oggpack_writeinit(b,bs);
  b->owner=bs;
}

static void _oggpack_extend(oggpack_buffer *b){
  if(b->head){
    b->head->length=b->head->buffer->size; /* only because ->begin is always
					      zero in a write buffer */
    b->count+=b->head->length;
    b->head=ogg_buffer_extend(b->head,OGGPACK_CHUNKSIZE);
  }else{
    b->head=b->tail=ogg_buffer_alloc(b->owner,OGGPACK_CHUNKSIZE);
  }

  b->headptr=b->head->buffer->data;
  b->headend=b->head->buffer->size;
}

/* Takes only up to 32 bits. */
void oggpack_write(oggpack_buffer *b,unsigned long value,int bits){

  value&=mask[bits]; 
  bits+=b->headbit;

  if(!b->headend){
    _oggpack_extend(b);
    *b->headptr=value;
  }else
    *b->headptr|=value<<b->headbit;  

  if(bits>=8){
    ++b->headptr;
    if(!--b->headend)_oggpack_extend(b);
    *b->headptr=value>>(8-b->headbit);  
    
    if(bits>=16){
      ++b->headptr;
      if(!--b->headend)_oggpack_extend(b);
      *b->headptr=value>>(16-b->headbit);  
      
      if(bits>=24){
	++b->headptr;
	if(!--b->headend)_oggpack_extend(b);
	*b->headptr=value>>(24-b->headbit);  
	
	if(bits>=32){
	  ++b->headptr;
	  if(!--b->headend)_oggpack_extend(b);
	  if(b->headbit)
	    *b->headptr=value>>(32-b->headbit);  
	  else
	    *b->headptr=0;
	}
      }
    }
  }
    
  b->headbit=bits&7;

}

/* Takes only up to 32 bits. */
void oggpackB_write(oggpack_buffer *b,unsigned long value,int bits){

  value=(value&mask[bits])<<(32-bits); 
  bits+=b->headbit;

  if(!b->headend){
    _oggpack_extend(b);
    *b->headptr=value>>24;
  }else
    *b->headptr|=value>>(24+b->headbit);    

  if(bits>=8){
    ++b->headptr;
    if(!--b->headend)_oggpack_extend(b);
    *b->headptr=value>>(16+b->headbit);  
    
    if(bits>=16){
      ++b->headptr;
      if(!--b->headend)_oggpack_extend(b);
      *b->headptr=value>>(8+b->headbit);  
      
      if(bits>=24){
	++b->headptr;
	if(!--b->headend)_oggpack_extend(b);
	*b->headptr=value>>(b->headbit);  
	
	if(bits>=32){
	  ++b->headptr;
	  if(!--b->headend)_oggpack_extend(b);
	  if(b->headbit)
	    *b->headptr=value<<(8-b->headbit);
	  else
	    *b->headptr=0;
	}
      }
    }
  }

  b->headbit=bits&7;
}

void oggpack_writealign(oggpack_buffer *b){
  int bits=8-b->headbit;
  if(bits<8)
    oggpack_write(b,0,bits);
}

void oggpackB_writealign(oggpack_buffer *b){
  int bits=8-b->headbit;
  if(bits<8)
    oggpackB_write(b,0,bits);
}

ogg_reference *oggpack_writebuffer(oggpack_buffer *b){
  /* unlike generic ogg_buffer_references, the oggpack write buffers
     will never have any potential prefixed/unused ogg_buffer data */

  b->head->length=b->headptr-b->head->buffer->data+(b->headbit+7)/8;
  return(b->tail);
}

ogg_reference *oggpackB_writebuffer(oggpack_buffer *b){
  return oggpack_writebuffer(b);
}

/* frees and deallocates the oggpack_buffer ogg_buffer usage */
void oggpack_writeclear(oggpack_buffer *b){
  ogg_buffer_release(b->tail);
  memset(b,0,sizeof(*b));
}

void oggpackB_writeclear(oggpack_buffer *b){
  oggpack_writeclear(b);
}

/* mark read process as having run off the end */
static void _adv_halt(oggpack_buffer *b){
  b->headptr=b->head->buffer->data+b->head->begin+b->head->length;
  b->headend=-1;
  b->headbit=0;
}

/* spans forward, skipping as many bytes as headend is negative; if
   headend is zero, simply finds next byte.  If we're up to the end
   of the buffer, leaves headend at zero.  If we've read past the end,
   halt the decode process. */
static void _span(oggpack_buffer *b){
  while(b->headend<1){
    if(b->head->next){
      b->count+=b->head->length;
      b->head=b->head->next;
      b->headptr=b->head->buffer->data+b->head->begin-b->headend; 
      b->headend+=b->head->length;      
    }else{
      /* we've either met the end of decode, or gone past it. halt
         only if we're past */
      if(b->headend<0 || b->headbit)
	/* read has fallen off the end */
	_adv_halt(b);

      break;
    }
  }
}

void oggpack_readinit(oggpack_buffer *b,ogg_reference *r){
  memset(b,0,sizeof(*b));

  b->tail=b->head=r;
  b->count=0;
  b->headptr=b->head->buffer->data+b->head->begin;
  b->headend=b->head->length;
  _span(b);
}

void oggpackB_readinit(oggpack_buffer *b,ogg_reference *r){
  oggpack_readinit(b,r);
}

#define _lookspan()   while(!end){\
                        head=head->next;\
                        if(!head) return -1;\
                        ptr=head->buffer->data + head->begin;\
                        end=head->length;\
                      }

/* Read in bits without advancing the bitptr; bits <= 32 */
int oggpack_look(oggpack_buffer *b,int bits,unsigned long *ret){
  unsigned long m=mask[bits];

  bits+=b->headbit;

  if(bits >= b->headend*8){
    int            end=b->headend;
    unsigned char *ptr=b->headptr;
    ogg_reference *head=b->head;

    if(end<0)return -1;
    
    if(bits){
      _lookspan();
      *ret=*ptr++>>b->headbit;
      if(bits>8){
	--end;
	_lookspan();
	*ret|=*ptr++<<(8-b->headbit);  
	if(bits>16){
	  --end;
	  _lookspan();
	  *ret|=*ptr++<<(16-b->headbit);  
	  if(bits>24){
	    --end;
	    _lookspan();
	    *ret|=*ptr++<<(24-b->headbit);  
	    if(bits>32 && b->headbit){
	      --end;
	      _lookspan();
	      *ret|=*ptr<<(32-b->headbit);
	    }
	  }
	}
      }
    }

  }else{

    /* make this a switch jump-table */
    *ret=b->headptr[0]>>b->headbit;
    if(bits>8){
      *ret|=b->headptr[1]<<(8-b->headbit);  
      if(bits>16){
	*ret|=b->headptr[2]<<(16-b->headbit);  
	if(bits>24){
	  *ret|=b->headptr[3]<<(24-b->headbit);  
	  if(bits>32 && b->headbit)
	    *ret|=b->headptr[4]<<(32-b->headbit);
	}
      }
    }
  }

  *ret&=m;
  return 0;
}

/* Read in bits without advancing the bitptr; bits <= 32 */
int oggpackB_look(oggpack_buffer *b,int bits,unsigned long *ret){
  int m=32-bits;

  bits+=b->headbit;

  if(bits >= b->headend<<3){
    int            end=b->headend;
    unsigned char *ptr=b->headptr;
    ogg_reference *head=b->head;

    if(end<0)return -1;
    
    if(bits){
      _lookspan();
      *ret=*ptr++<<(24+b->headbit);
      if(bits>8){
	--end;
	_lookspan();
	*ret|=*ptr++<<(16+b->headbit);   
	if(bits>16){
	  --end;
	  _lookspan();
	  *ret|=*ptr++<<(8+b->headbit);  
	  if(bits>24){
	    --end;
	    _lookspan();
	    *ret|=*ptr++<<(b->headbit);  
	    if(bits>32 && b->headbit){
	      --end;
	      _lookspan();
	      *ret|=*ptr>>(8-b->headbit);
	    }
	  }
	}
      }
    }
    
  }else{
  
    *ret=b->headptr[0]<<(24+b->headbit);
    if(bits>8){
      *ret|=b->headptr[1]<<(16+b->headbit);  
      if(bits>16){
	*ret|=b->headptr[2]<<(8+b->headbit);  
	if(bits>24){
	  *ret|=b->headptr[3]<<(b->headbit);  
	  if(bits>32 && b->headbit)
	    *ret|=b->headptr[4]>>(8-b->headbit);
	}
      }
    }
  }

  *ret>>=m;
  return 0;
}

long oggpack_look1(oggpack_buffer *b){
  if(b->headend<1)return -1;
  return (b->headptr[0]>>b->headbit)&1;
}

long oggpackB_look1(oggpack_buffer *b){
  if(b->headend<1)return -1;
  return (b->headptr[0]>>(7-b->headbit))&1;
}

/* limited to 32 at a time */
void oggpack_adv(oggpack_buffer *b,int bits){
  bits+=b->headbit;
  b->headend-=bits/8;
  b->headbit=bits&7;
  b->headptr+=bits/8;
  _span(b);
}

void oggpackB_adv(oggpack_buffer *b,int bits){
  oggpack_adv(b,bits);
}

/* spans forward and finds next byte.  Never halts */
static void _span_one(oggpack_buffer *b){
  while(b->headend<1){
    if(b->head->next){
      b->count+=b->head->length;
      b->head=b->head->next;
      b->headptr=b->head->buffer->data+b->head->begin; 
      b->headend=b->head->length;      
    }else
      break;
  }
}

void oggpack_adv1(oggpack_buffer *b){
  if(b->headend<2){
    if(b->headend<1){
      _adv_halt(b);
    }else{
      if(++b->headbit>7){
	b->headend--;
	b->headptr++;
	b->headbit=0;

	_span_one(b);
      }
    }
  }else{
    if(++b->headbit>7){
      b->headend--;
      b->headptr++;
      b->headbit=0;
    }
  }
}

void oggpackB_adv1(oggpack_buffer *b){
  oggpack_adv1(b);
}

static int _halt_one(oggpack_buffer *b){
  if(b->headend<1){
    _adv_halt(b);
    return -1;
  }
  return 0;
}

/* bits <= 32 */
int oggpack_read(oggpack_buffer *b,int bits,unsigned long *ret){
  unsigned long m=mask[bits];

  bits+=b->headbit;

  if(bits >= b->headend<<3){

    if(b->headend<0)return(-1);
    
    if(bits){
      if (_halt_one(b)) return -1;
      *ret=*b->headptr>>b->headbit;
      
      if(bits>=8){
	++b->headptr;
	--b->headend;
	_span_one(b);
	if(bits>8){
	  if (_halt_one(b)) return -1;
	  *ret|=*b->headptr<<(8-b->headbit);   
	  
	  if(bits>=16){
	    ++b->headptr;
	    --b->headend;
	    _span_one(b);
	    if(bits>16){
	      if (_halt_one(b)) return -1;
	      *ret|=*b->headptr<<(16-b->headbit);  
	      
	      if(bits>=24){
		++b->headptr;
		--b->headend;
		_span_one(b);
		if(bits>24){
		  if (_halt_one(b)) return -1;
		  *ret|=*b->headptr<<(24-b->headbit);
		  
		  if(bits>=32){
		    ++b->headptr;
		    --b->headend;
		    _span_one(b);
		    if(bits>32){
		      if (_halt_one(b)) return -1;
		      if(b->headbit)*ret|=*b->headptr<<(32-b->headbit);
		      
		    }
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  }else{
  
    *ret=b->headptr[0]>>b->headbit;
    if(bits>8){
      *ret|=b->headptr[1]<<(8-b->headbit);  
      if(bits>16){
	*ret|=b->headptr[2]<<(16-b->headbit);  
	if(bits>24){
	  *ret|=b->headptr[3]<<(24-b->headbit);  
	  if(bits>32 && b->headbit){
	    *ret|=b->headptr[4]<<(32-b->headbit);
	  }
	}
      }
    }
    
    b->headptr+=bits/8;
    b->headend-=bits/8;
  }

  *ret&=m;
  b->headbit=bits&7;   
  return(0);
}

/* bits <= 32 */
int oggpackB_read(oggpack_buffer *b,int bits,unsigned long *ret){
  long m=32-bits;
  
  bits+=b->headbit;

  if(bits >= b->headend<<3){

    if(b->headend<0)return(-1);

    if(bits){
      if (_halt_one(b)) return -1;
      *ret=*b->headptr<<(24+b->headbit);
      
      if(bits>=8){
	++b->headptr;
	--b->headend;
	_span_one(b);
	if(bits>8){
	  if (_halt_one(b)) return -1;
	  *ret|=*b->headptr<<(16+b->headbit);   
	  
	  if(bits>=16){
	    ++b->headptr;
	    --b->headend;
	    _span_one(b);
	    if(bits>16){
	      if (_halt_one(b)) return -1;
	      *ret|=*b->headptr<<(8+b->headbit);  
	      
	      if(bits>=24){
		++b->headptr;
		--b->headend;
		_span_one(b);
		if(bits>24){
		  if (_halt_one(b)) return -1;
		  *ret|=*b->headptr<<(b->headbit);  
		  
		  if(bits>=32){
		    ++b->headptr;
		    --b->headend;
		    _span_one(b);
		    if(bits>32){
		      if (_halt_one(b)) return -1;
		      if(b->headbit)*ret|=*b->headptr>>(8-b->headbit);
		      
		    }
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  }else{

    *ret=b->headptr[0]<<(24+b->headbit);
    if(bits>8){
      *ret|=b->headptr[1]<<(16+b->headbit);  
      if(bits>16){
	*ret|=b->headptr[2]<<(8+b->headbit);  
	if(bits>24){
	  *ret|=b->headptr[3]<<(b->headbit);  
	  if(bits>32 && b->headbit)
	    *ret|=b->headptr[4]>>(8-b->headbit);
	}
      }
    }

    b->headptr+=bits/8;
    b->headend-=bits/8;
  }

  b->headbit=bits&7;
  *ret>>=m;
  return 0;
}

long oggpack_read1(oggpack_buffer *b){
  long ret;

  if(b->headend<2){
    if (_halt_one(b)) return -1;

    ret=b->headptr[0]>>b->headbit++;
    b->headptr+=b->headbit/8;
    b->headend-=b->headbit/8;
    _span_one(b);
    
  }else{

    ret=b->headptr[0]>>b->headbit++;
    b->headptr+=b->headbit/8;
    b->headend-=b->headbit/8;

  }

  b->headbit&=7;   
  return ret&1;
}

long oggpackB_read1(oggpack_buffer *b){
  long ret;

  if(b->headend<2){
    if (_halt_one(b)) return -1;

    ret=b->headptr[0]>>(7-b->headbit++);
    b->headptr+=b->headbit/8;
    b->headend-=b->headbit/8;
    _span_one(b);
    
  }else{

    ret=b->headptr[0]>>(7-b->headbit++);
    b->headptr+=b->headbit/8;
    b->headend-=b->headbit/8;

  }

  b->headbit&=7;   
  return ret&1;
}

long oggpack_bytes(oggpack_buffer *b){
  return(b->count+b->headptr-b->head->buffer->data-b->head->begin+
	 (b->headbit+7)/8);
}

long oggpack_bits(oggpack_buffer *b){
  return((b->count+b->headptr-b->head->buffer->data-b->head->begin)*8+
	 b->headbit);
}

long oggpackB_bytes(oggpack_buffer *b){
  return oggpack_bytes(b);
}

long oggpackB_bits(oggpack_buffer *b){
  return oggpack_bits(b);
}
  

/* Self test of the bitwise routines; everything else is based on
   them, so they damned well better be solid. */

#ifdef _V_SELFTEST
#include <stdio.h>

static int ilog(unsigned int v){
  int ret=0;
  while(v){
    ret++;
    v>>=1;
  }
  return(ret);
}
      
oggpack_buffer o;
oggpack_buffer r;
ogg_buffer_state *bs;
ogg_reference *or;
#define TESTWORDS 4096

void report(char *in){
  fprintf(stderr,"%s",in);
  exit(1);
}

int getbyte(ogg_reference *or,int position){
  while(or && position>=or->length){
    position-=or->length;
    or=or->next;
    if(or==NULL){
      fprintf(stderr,"\n\tERROR: getbyte ran off end of buffer.\n");
      exit(1);
    }
  }

  return(or->buffer->data[position+or->begin]);
}

void cliptest(unsigned long *b,int vals,int bits,int *comp,int compsize){
  long bytes,i,bitcount=0;
  ogg_reference *or;

  oggpack_writeinit(&o,bs);
  for(i=0;i<vals;i++){
    oggpack_write(&o,b[i],bits?bits:ilog(b[i]));
    bitcount+=bits?bits:ilog(b[i]);
    if(bitcount!=oggpack_bits(&o)){
      report("wrong number of bits while writing!\n");
    }
    if((bitcount+7)/8!=oggpack_bytes(&o)){
      report("wrong number of bytes while writing!\n");
    }
  }

  or=oggpack_writebuffer(&o);
  bytes=oggpack_bytes(&o);

  if(bytes!=compsize)report("wrong number of bytes!\n");
  for(i=0;i<bytes;i++)if(getbyte(or,i)!=comp[i]){
    for(i=0;i<bytes;i++)fprintf(stderr,"%x %x\n",getbyte(or,i),(int)comp[i]);
    report("wrote incorrect value!\n");
  }

  bitcount=0;
  oggpack_readinit(&r,or);
  for(i=0;i<vals;i++){
    unsigned long test;
    int tbit=bits?bits:ilog(b[i]);
    if(oggpack_look(&r,tbit,&test))
      report("out of data!\n");
    if(test!=(b[i]&mask[tbit])){
      fprintf(stderr,"%ld) %lx %lx\n",i,(b[i]&mask[tbit]),test);
      report("looked at incorrect value!\n");
    }
    if(tbit==1)
      if(oggpack_look1(&r)!=(int)(b[i]&mask[tbit]))
	report("looked at single bit incorrect value!\n");
    if(tbit==1){
      if(oggpack_read1(&r)!=(int)(b[i]&mask[tbit]))
	report("read incorrect single bit value!\n");
    }else{
      if(oggpack_read(&r,tbit,&test)){
	report("premature end of data when reading!\n");
      }
      if(test!=(b[i]&mask[tbit])){
	fprintf(stderr,"%ld) %lx %lx\n",i,(b[i]&mask[tbit]),test);
	report("read incorrect value!\n");
      }
    }
    bitcount+=tbit;

    if(bitcount!=oggpack_bits(&r))
      report("wrong number of bits while reading!\n");
    if((bitcount+7)/8!=oggpack_bytes(&r))
      report("wrong number of bytes while reading!\n");

  }
  if(oggpack_bytes(&r)!=bytes)report("leftover bytes after read!\n");
  oggpack_writeclear(&o);

}

void cliptestB(unsigned long *b,int vals,int bits,int *comp,int compsize){
  long bytes,i;
  ogg_reference *or;

  oggpackB_writeinit(&o,bs);
  for(i=0;i<vals;i++)
    oggpackB_write(&o,b[i],bits?bits:ilog(b[i]));
  or=oggpackB_writebuffer(&o);
  bytes=oggpackB_bytes(&o);
  if(bytes!=compsize)report("wrong number of bytes!\n");
  for(i=0;i<bytes;i++)if(getbyte(or,i)!=comp[i]){
    for(i=0;i<bytes;i++)fprintf(stderr,"%x %x\n",getbyte(or,i),(int)comp[i]);
    report("wrote incorrect value!\n");
  }
  oggpackB_readinit(&r,or);
  for(i=0;i<vals;i++){
    unsigned long test;
    int tbit=bits?bits:ilog(b[i]);
    if(oggpackB_look(&r,tbit,&test))
      report("out of data!\n");
    if(test!=(b[i]&mask[tbit]))
      report("looked at incorrect value!\n");
    if(tbit==1)
      if(oggpackB_look1(&r)!=(int)(b[i]&mask[tbit]))
	report("looked at single bit incorrect value!\n");
    if(tbit==1){
      if(oggpackB_read1(&r)!=(int)(b[i]&mask[tbit]))
	report("read incorrect single bit value!\n");
    }else{
      if(oggpackB_read(&r,tbit,&test))
	report("premature end of data when reading!\n");
      if(test!=(b[i]&mask[tbit]))
	report("read incorrect value!\n");
    }
  }
  if(oggpackB_bytes(&r)!=bytes)report("leftover bytes after read!\n");
  oggpackB_writeclear(&o);
}

int flatten (unsigned char *flat){
  unsigned char *ptr=flat;
  ogg_reference *head=oggpack_writebuffer(&o);
  while(head){
    memcpy(ptr,head->buffer->data+head->begin,head->length);
    ptr+=head->length;
    head=head->next;
  }
  return ptr-flat;
}

void lsbverify(unsigned long *values,int *len,unsigned char *flat) {
  int j,k;
  int flatbyte=0;
  int flatbit=0;
  
  /* verify written buffer is correct bit-by-bit */
  for(j=0;j<TESTWORDS;j++){
    for(k=0;k<len[j];k++){
      int origbit=(values[j]>>k)&1;
      int bit=(flat[flatbyte]>>flatbit)&1;
      flatbit++;
      if(flatbit>7){
	flatbit=0;
	++flatbyte;
      }
      
      if(origbit!=bit){
	fprintf(stderr,"\n\tERROR: bit mismatch!  "
		"word %d, bit %d, value %lx, len %d\n",
		j,k,values[j],len[j]);
	exit(1);
      }
      
    }
  }

  /* verify that trailing packing is zeroed */
  while(flatbit && flatbit<8){
    int bit=(flat[flatbyte]>>flatbit++)&1;
  
    if(0!=bit){
      fprintf(stderr,"\n\tERROR: trailing byte padding not zero!\n");
      exit(1);
    }
  }  
  

}

void msbverify(unsigned long *values,int *len,unsigned char *flat) {
  int j,k;
  int flatbyte=0;
  int flatbit=0;
  
  /* verify written buffer is correct bit-by-bit */
  for(j=0;j<TESTWORDS;j++){
    for(k=0;k<len[j];k++){
      int origbit=(values[j]>>(len[j]-k-1))&1;
      int bit=(flat[flatbyte]>>(7-flatbit))&1;
      flatbit++;
      if(flatbit>7){
	flatbit=0;
	++flatbyte;
      }
      
      if(origbit!=bit){
	fprintf(stderr,"\n\tERROR: bit mismatch!  "
		"word %d, bit %d, value %lx, len %d\n",
		j,k,values[j],len[j]);
	exit(1);
      }
      
    }
  }

  /* verify that trailing packing is zeroed */
  while(flatbit && flatbit<8){
    int bit=(flat[flatbyte]>>(7-flatbit++))&1;
  
    if(0!=bit){
      fprintf(stderr,"\n\tERROR: trailing byte padding not zero!\n");
      exit(1);
    }
  }  
  
}

void _end_verify(int count){
  unsigned long temp;
  int i;

  /* are the proper number of bits left over? */
  int leftover=count*8-oggpack_bits(&o);
  if(leftover>7)
    report("\nERROR: too many bits reported left over.\n");
  
  /* does reading to exactly byte alignment *not* trip EOF? */
  if(oggpack_read(&o,leftover,&temp))
    report("\nERROR: read to but not past exact end tripped EOF.\n");
  if(oggpack_bits(&o)!=count*8)
    report("\nERROR: read to but not past exact end reported bad bitcount.\n");

  /* does EOF trip properly after a single additional bit? */
  if(!oggpack_read(&o,1,&temp))
    report("\nERROR: read past exact end did not trip EOF.\n");
  if(oggpack_bits(&o)!=count*8)
    report("\nERROR: read past exact end reported bad bitcount.\n");
  
  /* does EOF stay set over additional bit reads? */
  for(i=0;i<=32;i++){
    if(!oggpack_read(&o,i,&temp))
      report("\nERROR: EOF did not stay set on stream.\n");
    if(oggpack_bits(&o)!=count*8)
      report("\nERROR: read past exact end reported bad bitcount.\n");
  }
}	

void _end_verify2(int count){
  int i;

  /* are the proper number of bits left over? */
  int leftover=count*8-oggpack_bits(&o);
  if(leftover>7)
    report("\nERROR: too many bits reported left over.\n");
  
  /* does reading to exactly byte alignment *not* trip EOF? */
  oggpack_adv(&o,leftover);
  if(o.headend!=0)
    report("\nERROR: read to but not past exact end tripped EOF.\n");
  if(oggpack_bits(&o)!=count*8)
    report("\nERROR: read to but not past exact end reported bad bitcount.\n");
  
  /* does EOF trip properly after a single additional bit? */
  oggpack_adv(&o,1);
  if(o.headend>=0)
    report("\nERROR: read past exact end did not trip EOF.\n");
  if(oggpack_bits(&o)!=count*8)
    report("\nERROR: read past exact end reported bad bitcount.\n");
  
  /* does EOF stay set over additional bit reads? */
  for(i=0;i<=32;i++){
    oggpack_adv(&o,i);
    if(o.headend>=0)
      report("\nERROR: EOF did not stay set on stream.\n");
    if(oggpack_bits(&o)!=count*8)
      report("\nERROR: read past exact end reported bad bitcount.\n");
  }
}	

void _end_verify3(int count){
  unsigned long temp;
  int i;

  /* are the proper number of bits left over? */
  int leftover=count*8-oggpackB_bits(&o);
  if(leftover>7)
    report("\nERROR: too many bits reported left over.\n");
  
  /* does reading to exactly byte alignment *not* trip EOF? */
  if(oggpackB_read(&o,leftover,&temp))
    report("\nERROR: read to but not past exact end tripped EOF.\n");
  if(oggpackB_bits(&o)!=count*8)
    report("\nERROR: read to but not past exact end reported bad bitcount.\n");
  
  /* does EOF trip properly after a single additional bit? */
  if(!oggpackB_read(&o,1,&temp))
    report("\nERROR: read past exact end did not trip EOF.\n");
  if(oggpackB_bits(&o)!=count*8)
    report("\nERROR: read past exact end reported bad bitcount.\n");
  
  /* does EOF stay set over additional bit reads? */
  for(i=0;i<=32;i++){
    if(!oggpackB_read(&o,i,&temp))
      report("\nERROR: EOF did not stay set on stream.\n");
    if(oggpackB_bits(&o)!=count*8)
      report("\nERROR: read past exact end reported bad bitcount.\n");
  }
}	

void _end_verify4(int count){
  int i;

  /* are the proper number of bits left over? */
  int leftover=count*8-oggpackB_bits(&o);
  if(leftover>7)
    report("\nERROR: too many bits reported left over.\n");
  
  /* does reading to exactly byte alignment *not* trip EOF? */
  for(i=0;i<leftover;i++){
    oggpackB_adv1(&o);
    if(o.headend<0)
      report("\nERROR: read to but not past exact end tripped EOF.\n");
  }
  if(oggpackB_bits(&o)!=count*8)
    report("\nERROR: read to but not past exact end reported bad bitcount.\n");
  
  /* does EOF trip properly after a single additional bit? */
  oggpackB_adv1(&o);
  if(o.headend>=0)
    report("\nERROR: read past exact end did not trip EOF.\n");
  if(oggpackB_bits(&o)!=count*8)
    report("\nERROR: read past exact end reported bad bitcount.\n");
  
  /* does EOF stay set over additional bit reads? */
  for(i=0;i<=32;i++){
    oggpackB_adv1(&o);
    if(o.headend>=0)
      report("\nERROR: EOF did not stay set on stream.\n");
    if(oggpackB_bits(&o)!=count*8)
      report("\nERROR: read past exact end reported bad bitcount.\n");
  }
}	

int main(void){
  long bytes,i;
  static unsigned long testbuffer1[]=
    {18,12,103948,4325,543,76,432,52,3,65,4,56,32,42,34,21,1,23,32,546,456,7,
       567,56,8,8,55,3,52,342,341,4,265,7,67,86,2199,21,7,1,5,1,4};
  int test1size=43;

  static unsigned long testbuffer2[]=
    {216531625L,1237861823,56732452,131,3212421,12325343,34547562,12313212,
       1233432,534,5,346435231,14436467,7869299,76326614,167548585,
       85525151,0,12321,1,349528352};
  int test2size=21;

  static unsigned long testbuffer3[]=
    {1,0,14,0,1,0,12,0,1,0,0,0,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,1,1,1,1,0,0,1,
       0,1,30,1,1,1,0,0,1,0,0,0,12,0,11,0,1,0,0,1};
  int test3size=56;

  static unsigned long large[]=
    {2136531625L,2137861823,56732452,131,3212421,12325343,34547562,12313212,
       1233432,534,5,2146435231,14436467,7869299,76326614,167548585,
       85525151,0,12321,1,2146528352};

  int onesize=33;
  static int one[33]={146,25,44,151,195,15,153,176,233,131,196,65,85,172,47,40,
                    34,242,223,136,35,222,211,86,171,50,225,135,214,75,172,
                    223,4};
  static int oneB[33]={150,101,131,33,203,15,204,216,105,193,156,65,84,85,222,
		       8,139,145,227,126,34,55,244,171,85,100,39,195,173,18,
		       245,251,128};

  int twosize=6;
  static int two[6]={61,255,255,251,231,29};
  static int twoB[6]={247,63,255,253,249,120};

  int threesize=54;
  static int three[54]={169,2,232,252,91,132,156,36,89,13,123,176,144,32,254,
                      142,224,85,59,121,144,79,124,23,67,90,90,216,79,23,83,
                      58,135,196,61,55,129,183,54,101,100,170,37,127,126,10,
                      100,52,4,14,18,86,77,1};
  static int threeB[54]={206,128,42,153,57,8,183,251,13,89,36,30,32,144,183,
			 130,59,240,121,59,85,223,19,228,180,134,33,107,74,98,
			 233,253,196,135,63,2,110,114,50,155,90,127,37,170,104,
			 200,20,254,4,58,106,176,144,0};

  int foursize=38;
  static int four[38]={18,6,163,252,97,194,104,131,32,1,7,82,137,42,129,11,72,
                     132,60,220,112,8,196,109,64,179,86,9,137,195,208,122,169,
                     28,2,133,0,1};
  static int fourB[38]={36,48,102,83,243,24,52,7,4,35,132,10,145,21,2,93,2,41,
			1,219,184,16,33,184,54,149,170,132,18,30,29,98,229,67,
			129,10,4,32};

  int fivesize=45;
  static int five[45]={169,2,126,139,144,172,30,4,80,72,240,59,130,218,73,62,
                     241,24,210,44,4,20,0,248,116,49,135,100,110,130,181,169,
                     84,75,159,2,1,0,132,192,8,0,0,18,22};
  static int fiveB[45]={1,84,145,111,245,100,128,8,56,36,40,71,126,78,213,226,
			124,105,12,0,133,128,0,162,233,242,67,152,77,205,77,
			172,150,169,129,79,128,0,6,4,32,0,27,9,0};

  int sixsize=7;
  static int six[7]={17,177,170,242,169,19,148};
  static int sixB[7]={136,141,85,79,149,200,41};

  /* Test read/write together */
  /* Later we test against pregenerated bitstreams */
  bs=ogg_buffer_create();

  fprintf(stderr,"\nSmall preclipped packing (LSb): ");
  cliptest(testbuffer1,test1size,0,one,onesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nNull bit call (LSb): ");
  cliptest(testbuffer3,test3size,0,two,twosize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nLarge preclipped packing (LSb): ");
  cliptest(testbuffer2,test2size,0,three,threesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\n32 bit preclipped packing (LSb): ");

  oggpack_writeclear(&o);
  oggpack_writeinit(&o,bs);
  for(i=0;i<test2size;i++)
    oggpack_write(&o,large[i],32);
  or=oggpack_writebuffer(&o);
  bytes=oggpack_bytes(&o);
  oggpack_readinit(&r,or);
  for(i=0;i<test2size;i++){
    unsigned long test;
    if(oggpack_look(&r,32,&test)==-1)report("out of data. failed!");
    if(test!=large[i]){
      fprintf(stderr,"%ld != %ld (%lx!=%lx):",test,large[i],
	      test,large[i]);
      report("read incorrect value!\n");
    }
    oggpack_adv(&r,32);
  }
  if(oggpack_bytes(&r)!=bytes)report("leftover bytes after read!\n");
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nSmall unclipped packing (LSb): ");
  cliptest(testbuffer1,test1size,7,four,foursize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nLarge unclipped packing (LSb): ");
  cliptest(testbuffer2,test2size,17,five,fivesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nSingle bit unclipped packing (LSb): ");
  cliptest(testbuffer3,test3size,1,six,sixsize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nTesting read past end (LSb): ");
  {
    ogg_buffer lob={"\0\0\0\0\0\0\0\0",8,0,{0}};
    ogg_reference lor={&lob,0,8,0,1};

    oggpack_readinit(&r,&lor);
    for(i=0;i<64;i++){
      if(oggpack_read1(&r)!=0){
	fprintf(stderr,"failed; got -1 prematurely.\n");
	exit(1);
      }
    }
    if(oggpack_look1(&r)!=-1 ||
       oggpack_read1(&r)!=-1){
      fprintf(stderr,"failed; read past end without -1.\n");
      exit(1);
    }
  }
  {
    ogg_buffer lob={"\0\0\0\0\0\0\0\0",8,0,{0}};
    ogg_reference lor={&lob,0,8,0,1};
    unsigned long test;

    oggpack_readinit(&r,&lor);
    if(oggpack_read(&r,30,&test)!=0 || oggpack_read(&r,16,&test)!=0){
      fprintf(stderr,"failed 2; got -1 prematurely.\n");
      exit(1);
    }
    
    if(oggpack_look(&r,18,&test)!=0){
      fprintf(stderr,"failed 3; got -1 prematurely.\n");
      exit(1);
    }
    if(oggpack_look(&r,19,&test)!=-1){
      fprintf(stderr,"failed; read past end without -1.\n");
      exit(1);
    }
    if(oggpack_look(&r,32,&test)!=-1){
      fprintf(stderr,"failed; read past end without -1.\n");
      exit(1);
    }
  }
  fprintf(stderr,"ok.\n");
  oggpack_writeclear(&o);

  /********** lazy, cut-n-paste retest with MSb packing ***********/

  /* Test read/write together */
  /* Later we test against pregenerated bitstreams */

  fprintf(stderr,"\nSmall preclipped packing (MSb): ");
  cliptestB(testbuffer1,test1size,0,oneB,onesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nNull bit call (MSb): ");
  cliptestB(testbuffer3,test3size,0,twoB,twosize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nLarge preclipped packing (MSb): ");
  cliptestB(testbuffer2,test2size,0,threeB,threesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\n32 bit preclipped packing (MSb): ");

  oggpackB_writeinit(&o,bs);
  for(i=0;i<test2size;i++)
    oggpackB_write(&o,large[i],32);
  or=oggpackB_writebuffer(&o);
  bytes=oggpackB_bytes(&o);
  oggpackB_readinit(&r,or);
  for(i=0;i<test2size;i++){
    unsigned long test;
    if(oggpackB_look(&r,32,&test)==-1)report("out of data. failed!");
    if(test!=large[i]){
      fprintf(stderr,"%ld != %ld (%lx!=%lx):",test,large[i],
	      test,large[i]);
      report("read incorrect value!\n");
    }
    oggpackB_adv(&r,32);
  }
  if(oggpackB_bytes(&r)!=bytes)report("leftover bytes after read!\n");
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nSmall unclipped packing (MSb): ");
  cliptestB(testbuffer1,test1size,7,fourB,foursize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nLarge unclipped packing (MSb): ");
  cliptestB(testbuffer2,test2size,17,fiveB,fivesize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nSingle bit unclipped packing (MSb): ");
  cliptestB(testbuffer3,test3size,1,sixB,sixsize);
  fprintf(stderr,"ok.");

  fprintf(stderr,"\nTesting read past end (MSb): ");
  {
    ogg_buffer lob={"\0\0\0\0\0\0\0\0",8,0,{0}};
    ogg_reference lor={&lob,0,8,0,1};
    unsigned long test;

    oggpackB_readinit(&r,&lor);
    for(i=0;i<64;i++){
      if(oggpackB_read(&r,1,&test)){
	fprintf(stderr,"failed; got -1 prematurely.\n");
	exit(1);
      }
    }
    if(oggpackB_look(&r,1,&test)!=-1 ||
       oggpackB_read(&r,1,&test)!=-1){
      fprintf(stderr,"failed; read past end without -1.\n");
      exit(1);
    }
  }
  {
    ogg_buffer lob={"\0\0\0\0\0\0\0\0",8,0,{0}};
    ogg_reference lor={&lob,0,8,0,1};
    unsigned long test;
    oggpackB_readinit(&r,&lor);

    if(oggpackB_read(&r,30,&test)!=0 || oggpackB_read(&r,16,&test)!=0){
      fprintf(stderr,"failed 2; got -1 prematurely.\n");
      exit(1);
    }
    
    if(oggpackB_look(&r,18,&test)!=0){
      fprintf(stderr,"failed 3; got -1 prematurely.\n");
      exit(1);
    }
    if(oggpackB_look(&r,19,&test)!=-1){
      fprintf(stderr,"failed 4; read past end without -1.\n");
      exit(1);
    }
    if(oggpackB_look(&r,32,&test)!=-1){
      fprintf(stderr,"failed 5; read past end without -1.\n");
      exit(1);
    }
    fprintf(stderr,"ok.\n\n");
  }
  oggpackB_writeclear(&o);

  /* now the scary shit: randomized testing */

  for(i=0;i<10000;i++){
    int j,count=0,count2=0,bitcount=0;
    unsigned long values[TESTWORDS];
    int len[TESTWORDS];
    unsigned char flat[4*TESTWORDS]; /* max possible needed size */

    fprintf(stderr,"\rRandomized testing (LSb)... (%ld)   ",10000-i);
    oggpack_writeinit(&o,bs);

    /* generate a list of words and lengths */
    /* write the required number of bits out to packbuffer */
    for(j=0;j<TESTWORDS;j++){
      values[j]=rand();
      len[j]=(rand()%33);

      oggpack_write(&o,values[j],len[j]);

      bitcount+=len[j];
      if(oggpack_bits(&o)!=bitcount){
	fprintf(stderr,"\nERROR: Write bitcounter %d != %ld!\n",
		bitcount,oggpack_bits(&o));
	exit(1);
      }
      if(oggpack_bytes(&o)!=(bitcount+7)/8){
	fprintf(stderr,"\nERROR: Write bytecounter %d != %ld!\n",
		(bitcount+7)/8,oggpack_bytes(&o));
	exit(1);
      }
      
    }

    /* flatten the packbuffer out to a vector */
    count2=flatten(flat);
    if(count2<(bitcount+7)/8){
      fprintf(stderr,"\nERROR: flattened write buffer incorrect length\n");
      exit(1);
    }
    oggpack_writeclear(&o);

    /* verify against original list */
    lsbverify(values,len,flat);

    /* construct random-length buffer chain from flat vector; random
       byte starting offset within the length of the vector */
    {
      ogg_reference *or=NULL,*orl=NULL;
      unsigned char *ptr=flat;
      
      /* build buffer chain */
      while(count2){
	int ilen=(rand()%32);
	int ibegin=(rand()%32);

	if(ilen>count2)ilen=count2;

	if(or)
	  orl=ogg_buffer_extend(orl,ilen+ibegin);
	else
	  or=orl=ogg_buffer_alloc(bs,ilen+ibegin);

	memcpy(orl->buffer->data+ibegin,ptr,ilen);
       
	orl->length=ilen;
	orl->begin=ibegin;

	count2-=ilen;
	ptr+=ilen;
      }

      if(ogg_buffer_length(or)!=(bitcount+7)/8){
	fprintf(stderr,"\nERROR: buffer length incorrect after build.\n");
	exit(1);
      }


      {
	int begin=(rand()%TESTWORDS);
	int ilen=(rand()%(TESTWORDS-begin));
	int bitoffset,bitcount=0;
	unsigned long temp;

	for(j=0;j<begin;j++)
	  bitcount+=len[j];
	or=ogg_buffer_pretruncate(or,bitcount/8);
	bitoffset=bitcount%=8;
	for(;j<begin+ilen;j++)
	  bitcount+=len[j];
	ogg_buffer_posttruncate(or,((bitcount+7)/8));

	if((count=ogg_buffer_length(or))!=(bitcount+7)/8){
	  fprintf(stderr,"\nERROR: buffer length incorrect after truncate.\n");
	  exit(1);
	}
	
	oggpack_readinit(&o,or);

	/* verify bit count */
	if(oggpack_bits(&o)!=0){
	  fprintf(stderr,"\nERROR: Read bitcounter not zero!\n");
	  exit(1);
	}
	if(oggpack_bytes(&o)!=0){
	  fprintf(stderr,"\nERROR: Read bytecounter not zero!\n");
	  exit(1);
	}

	bitcount=bitoffset;
	oggpack_read(&o,bitoffset,&temp);

	/* read and compare to original list */
	for(j=begin;j<begin+ilen;j++){
	  int ret;
	  if(len[j]==1 && rand()%1)
	    temp=ret=oggpack_read1(&o);
	  else
	    ret=oggpack_read(&o,len[j],&temp);
	  if(ret<0){
	    fprintf(stderr,"\nERROR: End of stream too soon! word: %d,%d\n",
		    j-begin,ilen);
	    exit(1);
	  }
	  if(temp!=(values[j]&mask[len[j]])){
	    fprintf(stderr,"\nERROR: Incorrect read %lx != %lx, word %d, len %d\n",
		    values[j]&mask[len[j]],temp,j-begin,len[j]);
	    exit(1);
	  }
	  bitcount+=len[j];
	  if(oggpack_bits(&o)!=bitcount){
	    fprintf(stderr,"\nERROR: Read bitcounter %d != %ld!\n",
		    bitcount,oggpack_bits(&o));
	    exit(1);
	  }
	  if(oggpack_bytes(&o)!=(bitcount+7)/8){
	    fprintf(stderr,"\nERROR: Read bytecounter %d != %ld!\n",
		    (bitcount+7)/8,oggpack_bytes(&o));
	    exit(1);
	  }
	  
	}
	_end_verify(count);
	
	/* look/adv version */
	oggpack_readinit(&o,or);
	bitcount=bitoffset;
	oggpack_adv(&o,bitoffset);

	/* read and compare to original list */
	for(j=begin;j<begin+ilen;j++){
	  int ret;
	  if(len[j]==1 && rand()%1)
	    temp=ret=oggpack_look1(&o);
	  else
	    ret=oggpack_look(&o,len[j],&temp);

	  if(ret<0){
	    fprintf(stderr,"\nERROR: End of stream too soon! word: %d\n",
		    j-begin);
	    exit(1);
	  }
	  if(temp!=(values[j]&mask[len[j]])){
	    fprintf(stderr,"\nERROR: Incorrect look %lx != %lx, word %d, len %d\n",
		    values[j]&mask[len[j]],temp,j-begin,len[j]);
	    exit(1);
	  }
	  if(len[j]==1 && rand()%1)
	    oggpack_adv1(&o);
	  else
	    oggpack_adv(&o,len[j]);
	  bitcount+=len[j];
	  if(oggpack_bits(&o)!=bitcount){
	    fprintf(stderr,"\nERROR: Look/Adv bitcounter %d != %ld!\n",
		    bitcount,oggpack_bits(&o));
	    exit(1);
	  }
	  if(oggpack_bytes(&o)!=(bitcount+7)/8){
	    fprintf(stderr,"\nERROR: Look/Adv bytecounter %d != %ld!\n",
		    (bitcount+7)/8,oggpack_bytes(&o));
	    exit(1);
	  }
	  
	}
	_end_verify2(count);

      }
      ogg_buffer_release(or);
    }
  }
  fprintf(stderr,"\rRandomized testing (LSb)... ok.   \n");

  /* cut & paste: lazy bastahd alert */

  for(i=0;i<10000;i++){
    int j,count,count2=0,bitcount=0;
    unsigned long values[TESTWORDS];
    int len[TESTWORDS];
    unsigned char flat[4*TESTWORDS]; /* max possible needed size */

    fprintf(stderr,"\rRandomized testing (MSb)... (%ld)   ",10000-i);
    oggpackB_writeinit(&o,bs);

    /* generate a list of words and lengths */
    /* write the required number of bits out to packbuffer */
    for(j=0;j<TESTWORDS;j++){
      values[j]=rand();
      len[j]=(rand()%33);

      oggpackB_write(&o,values[j],len[j]);

      bitcount+=len[j];
      if(oggpackB_bits(&o)!=bitcount){
	fprintf(stderr,"\nERROR: Write bitcounter %d != %ld!\n",
		bitcount,oggpackB_bits(&o));
	exit(1);
      }
      if(oggpackB_bytes(&o)!=(bitcount+7)/8){
	fprintf(stderr,"\nERROR: Write bytecounter %d != %ld!\n",
		(bitcount+7)/8,oggpackB_bytes(&o));
	exit(1);
      }
      
    }

    /* flatten the packbuffer out to a vector */
    count2=flatten(flat);
    if(count2<(bitcount+7)/8){
      fprintf(stderr,"\nERROR: flattened write buffer incorrect length\n");
      exit(1);
    }
    oggpackB_writeclear(&o);

    /* verify against original list */
    msbverify(values,len,flat);

    /* construct random-length buffer chain from flat vector; random
       byte starting offset within the length of the vector */
    {
      ogg_reference *or=NULL,*orl=NULL;
      unsigned char *ptr=flat;
      
      /* build buffer chain */
      while(count2){
	int ilen=(rand()%32);
	int ibegin=(rand()%32);

	if(ilen>count2)ilen=count2;

	if(or)
	  orl=ogg_buffer_extend(orl,ilen+ibegin);
	else
	  or=orl=ogg_buffer_alloc(bs,ilen+ibegin);

	memcpy(orl->buffer->data+ibegin,ptr,ilen);
       
	orl->length=ilen;
	orl->begin=ibegin;

	count2-=ilen;
	ptr+=ilen;
      }

      if(ogg_buffer_length(or)!=(bitcount+7)/8){
	fprintf(stderr,"\nERROR: buffer length incorrect after build.\n");
	exit(1);
      }


      {
	int begin=(rand()%TESTWORDS);
	int ilen=(rand()%(TESTWORDS-begin));
	int bitoffset,bitcount=0;
	unsigned long temp;

	for(j=0;j<begin;j++)
	  bitcount+=len[j];
	/* also exercise the split code */
	{
	  ogg_reference *temp=ogg_buffer_split(&or,0,bitcount/8);
	  ogg_buffer_release(temp);
	}

	bitoffset=bitcount%=8;
	for(;j<begin+ilen;j++)
	  bitcount+=len[j];
	ogg_buffer_posttruncate(or,((bitcount+7)/8));

	if((count=ogg_buffer_length(or))!=(bitcount+7)/8){
	  fprintf(stderr,"\nERROR: buffer length incorrect after truncate.\n");
	  exit(1);
	}
	
	oggpackB_readinit(&o,or);

	/* verify bit count */
	if(oggpackB_bits(&o)!=0){
	  fprintf(stderr,"\nERROR: Read bitcounter not zero!\n");
	  exit(1);
	}
	if(oggpackB_bytes(&o)!=0){
	  fprintf(stderr,"\nERROR: Read bytecounter not zero!\n");
	  exit(1);
	}

	bitcount=bitoffset;
	oggpackB_read(&o,bitoffset,&temp);

	/* read and compare to original list */
	for(j=begin;j<begin+ilen;j++){
	  int ret;
	  if(len[j]==1 && rand()%1)
	    temp=ret=oggpackB_read1(&o);
	  else
	    ret=oggpackB_read(&o,len[j],&temp);
	  if(ret<0){
	    fprintf(stderr,"\nERROR: End of stream too soon! word: %d,%d\n",
		    j-begin,ilen);
	    exit(1);
	  }
	  if(temp!=(values[j]&mask[len[j]])){
	    fprintf(stderr,"\nERROR: Incorrect read %lx != %lx, word %d, len %d\n",
		    values[j]&mask[len[j]],temp,j-begin,len[j]);
	    exit(1);
	  }
	  bitcount+=len[j];
	  if(oggpackB_bits(&o)!=bitcount){
	    fprintf(stderr,"\nERROR: Read bitcounter %d != %ld!\n",
		    bitcount,oggpackB_bits(&o));
	    exit(1);
	  }
	  if(oggpackB_bytes(&o)!=(bitcount+7)/8){
	    fprintf(stderr,"\nERROR: Read bytecounter %d != %ld!\n",
		    (bitcount+7)/8,oggpackB_bytes(&o));
	    exit(1);
	  }
	  
	}
	_end_verify3(count);

	/* look/adv version */
	oggpackB_readinit(&o,or);
	bitcount=bitoffset;
	oggpackB_adv(&o,bitoffset);

	/* read and compare to original list */
	for(j=begin;j<begin+ilen;j++){
	  int ret;
	  if(len[j]==1 && rand()%1)
	    temp=ret=oggpackB_look1(&o);
	  else
	    ret=oggpackB_look(&o,len[j],&temp);

	  if(ret<0){
	    fprintf(stderr,"\nERROR: End of stream too soon! word: %d\n",
		    j-begin);
	    exit(1);
	  }
	  if(temp!=(values[j]&mask[len[j]])){
	    fprintf(stderr,"\nERROR: Incorrect look %lx != %lx, word %d, len %d\n",
		    values[j]&mask[len[j]],temp,j-begin,len[j]);
	    exit(1);
	  }
	  if(len[j]==1 && rand()%1)
	    oggpackB_adv1(&o);
	  else
	    oggpackB_adv(&o,len[j]);
	  bitcount+=len[j];
	  if(oggpackB_bits(&o)!=bitcount){
	    fprintf(stderr,"\nERROR: Look/Adv bitcounter %d != %ld!\n",
		    bitcount,oggpackB_bits(&o));
	    exit(1);
	  }
	  if(oggpackB_bytes(&o)!=(bitcount+7)/8){
	    fprintf(stderr,"\nERROR: Look/Adv bytecounter %d != %ld!\n",
		    (bitcount+7)/8,oggpackB_bytes(&o));
	    exit(1);
	  }
	  
	}
	_end_verify4(count);

      }
      ogg_buffer_release(or);
    }
  }
  fprintf(stderr,"\rRandomized testing (MSb)... ok.   \n");

  ogg_buffer_destroy(bs);
  return(0);
}  
#endif  /* _V_SELFTEST */
