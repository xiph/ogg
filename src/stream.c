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

 function: code raw packets into framed Ogg logical stream and
           decode Ogg logical streams back into raw packets
 last mod: $Id: stream.c,v 1.1.2.10 2003/03/28 04:51:33 xiphmont Exp $

 ********************************************************************/

#include <stdlib.h>
#include <string.h>
#include "ogginternal.h" /* proper way to suck in ogg/ogg.h from a
			    libogg compile */

/* A complete description of Ogg framing exists in docs/framing.html */

ogg_stream_state *ogg_stream_create(int serialno){
  ogg_stream_state *os=_ogg_calloc(1,sizeof(*os));
  os->watermark=4096;
  os->serialno=serialno;
  os->bufferpool=ogg_buffer_create();
  return os;
} 

int ogg_stream_setfill(ogg_stream_state *os,int watermark){
  if(os){
    if(watermark>65535)watermark=65535;
    os->watermark=watermark;
    return watermark;
  }
  return OGG_EINVAL;
} 

/* _clear does not free os, only the non-flat storage within */
int ogg_stream_destroy(ogg_stream_state *os){
  if(os){

    ogg_buffer_release(os->header_tail);
    ogg_buffer_release(os->body_tail);
    oggbyte_clear(&os->header_build);
    ogg_buffer_destroy(os->bufferpool);
    memset(os,0,sizeof(*os));    

  }
  return OGG_SUCCESS;
} 

/* finish building a header then flush the current packet header and
   body to the output buffer */
static void _packet_flush(ogg_stream_state *os,int nextcomplete){
  oggbyte_buffer *obb=&os->header_build;
  unsigned char ctemp;

  if(os->lacing_fill){
    /* build header */
    oggbyte_set1(obb,'O',0);
    oggbyte_set1(obb,'g',1);
    oggbyte_set1(obb,'g',2);
    oggbyte_set1(obb,'S',3);

    oggbyte_set1(obb,0x00,4);   /* stream structure version */
     
    ctemp=0x00;
    if(os->continued)ctemp|=0x01; /* continued packet flag? */
    os->continued=nextcomplete;
    if(os->b_o_s==0)ctemp|=0x02;  /* first page flag? */
    if(os->e_o_s)ctemp|=0x04;     /* last page flag? */
    oggbyte_set1(obb,ctemp,5);

    /* 64 bits of PCM position */
    if(!os->b_o_s)
      oggbyte_set8(obb,0,6);
    else
      oggbyte_set8(obb,os->granulepos,6);
    os->b_o_s=1;
    
    
    /* 32 bits of stream serial number */
    oggbyte_set4(obb,os->serialno,14);
    
    /* 32 bits of page counter (we have both counter and page header
       because this val can roll over) */
    if(os->pageno==-1)os->pageno=0; /* because someone called
				       stream_reset; this would be a
				       strange thing to do in an
				       encode stream, but it has
				       plausible uses */
    oggbyte_set4(obb,os->pageno++,18);
    
    /* CRC filled in later */
    /* segment table size */
    oggbyte_set1(obb,os->lacing_fill,26);

    /* toss the header on the fifo */
    if(os->header_tail){
      ogg_reference *ret=oggbyte_return_and_reset(&os->header_build);
      os->header_head=ogg_buffer_cat(os->header_head,ret);
      if(nextcomplete)oggbyte_init(&os->header_build,0,os->bufferpool);
    }else{
      os->header_tail=oggbyte_return_and_reset(&os->header_build);
      os->header_head=ogg_buffer_walk(os->header_tail);
      if(nextcomplete)oggbyte_init(&os->header_build,0,os->bufferpool);
    }
    os->lacing_fill=0;
    os->body_fill=0;
  }
}

/* submit data to the internal buffer of the framing engine */
int ogg_stream_packetin(ogg_stream_state *os,ogg_packet *op){
  /* get sizing */
  long bytes=ogg_buffer_length(op->packet);
  long lacing_vals=bytes/255+1;
  int  remainder=bytes%255;
  int  i;

  if(os->e_o_s)return OGG_EEOS;
  
  if(!os->lacing_fill)
    oggbyte_init(&os->header_build,0,os->bufferpool);

  /* concat packet data */
  if(os->body_head)
    os->body_head=ogg_buffer_cat(os->body_head,op->packet);
  else
    os->body_tail=os->body_head=op->packet;

  /* add lacing vals, but finish/flush packet first if we hit a
     (watermark && not initial page) */
  for(i=0;i<lacing_vals-1;i++){ /* handle the 255s first */
    os->body_fill+=255;
    oggbyte_set1(&os->header_build,255,27+os->lacing_fill++);
    
    if(os->body_fill>=os->watermark && os->b_o_s)_packet_flush(os,1);
    if(os->lacing_fill==255)_packet_flush(os,1);
  }

  /* we know we'll finish this packet on this page; propogate
     granulepos et al and then finish packet lacing */
  
  os->body_fill+=remainder;
  os->granulepos=op->granulepos;
  os->packetno++;  /* for the sake of completeness */
  if(op->e_o_s)os->e_o_s=1;
  oggbyte_set1(&os->header_build,remainder,27+os->lacing_fill++);

  if(os->e_o_s || 
     os->body_fill>=os->watermark ||
     !os->b_o_s ||
     os->lacing_fill==255)_packet_flush(os,0);
  
  return OGG_SUCCESS;
}

/* This constructs pages from buffered packet segments. */
int ogg_stream_pageout(ogg_stream_state *os, ogg_page *og){
  oggbyte_buffer ob;
  long header_bytes;
  long body_bytes=0;
  int i;

  /* is there a page waiting to come back? */
  if(!os->header_tail) return 0;

  /* get header and body sizes */
  oggbyte_init(&ob,os->header_tail,0);
  header_bytes=oggbyte_read1(&ob,26)+27;
  for(i=27;i<header_bytes;i++)
    body_bytes+=oggbyte_read1(&ob,i);

  /* split page references out of the fifos */
  if(og){
    og->header=ogg_buffer_split(&os->header_tail,&os->header_head,header_bytes);
    og->body=ogg_buffer_split(&os->body_tail,&os->body_head,body_bytes);

    /* checksum */
    ogg_page_checksum_set(og);
  }else{
    os->header_tail=ogg_buffer_pretruncate(os->header_tail,header_bytes);
    os->body_tail=ogg_buffer_pretruncate(os->body_tail,body_bytes);
    if(!os->header_tail)os->header_head=0;
    if(!os->body_tail)os->body_head=0;
  }
  
  return 1;
}

/* This will flush remaining packets into a page (returning nonzero),
   even if there is not enough data to trigger a flush normally
   (undersized page). If there are no packets or partial packets to
   flush, ogg_stream_flush returns 0.  Note that ogg_stream_flush will
   try to flush a normal sized page like ogg_stream_pageout; a call to
   ogg_stream_flush does not guarantee that all packets have flushed.
   Only a return value of 0 from ogg_stream_flush indicates all packet
   data is flushed into pages.

   since ogg_stream_flush will flush the last page in a stream even if
   it's undersized, you almost certainly want to use ogg_stream_pageout
   (and *not* ogg_stream_flush) unless you specifically need to flush 
   an page regardless of size in the middle of a stream. */

int ogg_stream_flush(ogg_stream_state *os,ogg_page *og){
  
  /* If there's no page already waiting for output, flush a partial
     page... assuming we have one */
  if(!os->header_tail)_packet_flush(os,0);
  return ogg_stream_pageout(os,og);
  
}

int ogg_stream_eos(ogg_stream_state *os){
  return os->e_o_s;
}

/* DECODING PRIMITIVES: packet streaming layer **********************/

#define FINFLAG 0x80000000UL
#define FINMASK 0x7fffffffUL

static void _next_lace(oggbyte_buffer *ob,ogg_stream_state *os){
  /* search ahead one lace */
  os->body_fill_next=0;
  while(os->laceptr<os->lacing_fill){
    int val=oggbyte_read1(ob,27+os->laceptr++);
    os->body_fill_next+=val;
    if(val<255){
      os->body_fill_next|=FINFLAG;
      os->clearflag=1;
      break;
    }
  }
}

/* sync and reporting within a logical stream uses a flagging system
   to improve the utility of the information coming back.  There are
   two basic problems a stream can run into; missing pages (a hole in
   the page sequence numbering), and malformed pages such that
   spanning isn't handled properly.  Both need to be reported.

   OGG_EHOLE happens when a page is out of sequence.  However, this
   can be a natural case after seeking or reset and we want to
   suppress the error in this case.  Nor shuld the error be reported
   redundantly. We need to *set* the hole flag (see below), but we
   don't want to report it.  0==unset. 1==set, 2==set and report.

   OGG_ESPAN happens when packet span is indicated but there's no
   spanning packet data, or there's spanning packet data and no
   declared span.  Naturally, this error should also not be
   mistriggered due to seek or reset, or reported redundantly. */

static void _span_queued_page(ogg_stream_state *os){ 
  while( !(os->body_fill&FINFLAG) ){
    
    if(!os->header_tail)break;

    /* first flush out preceeding page header (if any).  Body is
       flushed as it's consumed, so that's not done here. */

    if(os->lacing_fill>=0)
      os->header_tail=ogg_buffer_pretruncate(os->header_tail,
					     os->lacing_fill+27);
    os->lacing_fill=0;
    os->laceptr=0;
    os->clearflag=0;

    if(!os->header_tail){
      os->header_head=0;
      break;
    }else{
      
      /* process/prepare next page, if any */

      ogg_page og;               /* only for parsing header values */
      og.header=os->header_tail; /* only for parsing header values */
      long pageno=ogg_page_pageno(&og);
      oggbyte_buffer ob;

      oggbyte_init(&ob,os->header_tail,0);
      os->lacing_fill=oggbyte_read1(&ob,26);
      
      /* are we in sequence? */
      if(pageno!=os->pageno){
	if(os->pageno==-1) /* indicates seek or reset */
	  os->holeflag=1;  /* set for internal use */
	else
	  os->holeflag=2;  /* set for external reporting */

	os->body_tail=ogg_buffer_pretruncate(os->body_tail,
					     os->body_fill);
	if(os->body_tail==0)os->body_head=0;
	os->body_fill=0;

      }
    
      if(ogg_page_continued(&og)){
	if(os->body_fill==0){
	  /* continued packet, but no preceeding data to continue */
	  /* dump the first partial packet on the page */
	  _next_lace(&ob,os);	
	  os->body_tail=
	    ogg_buffer_pretruncate(os->body_tail,os->body_fill_next&FINMASK);
	  if(os->body_tail==0)os->body_head=0;
	  /* set span flag */
	  if(!os->spanflag && !os->holeflag)os->spanflag=2;
	}
      }else{
	if(os->body_fill>0){
	  /* preceeding data to continue, but not a continued page */
	  /* dump body_fill */
	  os->body_tail=ogg_buffer_pretruncate(os->body_tail,
					       os->body_fill);
	  if(os->body_tail==0)os->body_head=0;
	  os->body_fill=0;

	  /* set espan flag */
	  if(!os->spanflag && !os->holeflag)os->spanflag=2;
	}
      }

      if(os->laceptr<os->lacing_fill){
	os->granulepos=ogg_page_granulepos(&og);

	/* get current packet size & flag */
	_next_lace(&ob,os);
	os->body_fill+=os->body_fill_next; /* addition handles the flag fine;
					     unsigned on purpose */
	/* ...and next packet size & flag */
	_next_lace(&ob,os);

      }
      
      os->pageno=pageno+1;
      os->e_o_s=ogg_page_eos(&og);
      os->b_o_s=ogg_page_bos(&og);
    
    }
  }
}

/* add the incoming page to the stream state; we decompose the page
   into packet segments here as well. */

int ogg_stream_pagein(ogg_stream_state *os, ogg_page *og){

  int serialno=ogg_page_serialno(og);
  int version=ogg_page_version(og);

  /* check the serial number */
  if(serialno!=os->serialno)return OGG_ESERIAL;
  if(version>0)return OGG_EVERSION ;

  /* add to fifos */
  if(!os->body_tail){
    os->body_tail=og->body;
    os->body_head=ogg_buffer_walk(og->body);
  }else{
    os->body_head=ogg_buffer_cat(os->body_head,og->body);
  }
  if(!os->header_tail){
    os->header_tail=og->header;
    os->header_head=ogg_buffer_walk(og->header);
    os->lacing_fill=-27;
  }else{
    os->header_head=ogg_buffer_cat(os->header_head,og->header);
  }

  return OGG_SUCCESS;
}

int ogg_stream_reset(ogg_stream_state *os){

  ogg_buffer_release(os->header_tail);
  ogg_buffer_release(os->body_tail);
  os->header_tail=os->header_head=0;
  os->body_tail=os->body_head=0;

  os->e_o_s=0;
  os->b_o_s=0;
  os->pageno=-1;
  os->packetno=0;
  os->granulepos=0;

  os->body_fill=0;
  os->lacing_fill=0;
  oggbyte_clear(&os->header_build);

  os->holeflag=0;
  os->spanflag=0;
  os->clearflag=0;
  os->laceptr=0;
  os->body_fill_next=0;

  return OGG_SUCCESS;
}

int ogg_stream_reset_serialno(ogg_stream_state *os,int serialno){
  ogg_stream_reset(os);
  os->serialno=serialno;
  return OGG_SUCCESS;
}

static int _packetout(ogg_stream_state *os,ogg_packet *op,int adv){

  _span_queued_page(os);

  if(os->holeflag){
    int temp=os->holeflag;
    if(os->clearflag)
      os->holeflag=0;
    else
      os->holeflag=1;
    if(temp==2){
      os->packetno++;
      return OGG_HOLE;
    }
  }
  if(os->spanflag){
    int temp=os->spanflag;
    if(os->clearflag)
      os->spanflag=0;
    else
      os->spanflag=1;
    if(temp==2){
      os->packetno++;
      return OGG_SPAN;
    }
  }

  if(!(os->body_fill&FINFLAG))return 0;
  if(!op && !adv)return 1; /* just using peek as an inexpensive way
                               to ask if there's a whole packet
                               waiting */
  if(op){
    op->b_o_s=os->b_o_s;
    if(os->e_o_s && os->body_fill_next==0)
      op->e_o_s=os->e_o_s;
    else
      op->e_o_s=0;
    if( (os->body_fill&FINFLAG) && !(os->body_fill_next&FINFLAG) )
      op->granulepos=os->granulepos;
    else
      op->granulepos=-1;
    op->packetno=os->packetno;
  }

  if(adv){
    oggbyte_buffer ob;
    oggbyte_init(&ob,os->header_tail,0);

    /* split the body contents off */
    if(op){
      op->packet=ogg_buffer_split(&os->body_tail,&os->body_head,os->body_fill&FINMASK);
    }else{
      os->body_tail=ogg_buffer_pretruncate(os->body_tail,os->body_fill&FINMASK);
      if(os->body_tail==0)os->body_head=0;
    }

    /* update lacing pointers */
    os->body_fill=os->body_fill_next;
    _next_lace(&ob,os);
  }else{
    if(op)op->packet=ogg_buffer_dup(os->body_tail,0,os->body_fill&FINMASK);
  }
  
  if(adv){
    os->packetno++;
    os->b_o_s=0;
  }

  return 1;
}

int ogg_stream_packetout(ogg_stream_state *os,ogg_packet *op){
  return _packetout(os,op,1);
}

int ogg_stream_packetpeek(ogg_stream_state *os,ogg_packet *op){
  return _packetout(os,op,0);
}

int ogg_packet_release(ogg_packet *op) {
  ogg_buffer_release(op->packet);
  memset(op, 0, sizeof(*op));
  return OGG_SUCCESS;
}

int ogg_page_release(ogg_page *og) {
  ogg_buffer_release(og->header);
  ogg_buffer_release(og->body);
  memset(og, 0, sizeof(*og));
  return OGG_SUCCESS;
}

#ifdef _V_SELFTEST2
#include <stdio.h>

ogg_stream_state *os_en, *os_de;
ogg_sync_state *oy;
ogg_buffer_state *bs;

void checkpacket(ogg_packet *op,int len, int no, int pos){
  long j;
  static int sequence=0;
  static int lastno=0;
  oggbyte_buffer ob;

  if(ogg_buffer_length(op->packet)!=len){
    fprintf(stderr,"incorrect packet length!\n");
    exit(1);
  }
  if(op->granulepos!=pos){
    fprintf(stderr,"incorrect packet position!\n");
    exit(1);
  }

  /* packet number just follows sequence/gap; adjust the input number
     for that */
  if(no==0){
    sequence=0;
  }else{
    sequence++;
    if(no>lastno+1)
      sequence++;
  }
  lastno=no;
  if(op->packetno!=sequence){
    fprintf(stderr,"incorrect packet sequence %ld != %d\n",
	    (long)(op->packetno),sequence);
    exit(1);
  }

  /* Test data */
  oggbyte_init(&ob,op->packet,0);
  for(j=0;j<ogg_buffer_length(op->packet);j++)
    if(oggbyte_read1(&ob,j)!=((j+no)&0xff)){
      fprintf(stderr,"body data mismatch (1) at pos %ld: %x!=%lx!\n\n",
	      j,oggbyte_read1(&ob,j),(j+no)&0xff);
      exit(1);
    }
}

void check_page(unsigned char *data,const int *header,ogg_page *og){
  long j;
  oggbyte_buffer ob;

  /* test buffer lengths */
  long header_len=header[26]+27;
  long body_len=0;

  for(j=27;j<header_len;j++)
    body_len+=header[j];

  if(header_len!=ogg_buffer_length(og->header)){
    fprintf(stderr,"page header length mismatch: %ld correct, buffer is %ld\n",
	    header_len,ogg_buffer_length(og->header));
    exit(1);
  }
  if(body_len!=ogg_buffer_length(og->body)){
    fprintf(stderr,"page body length mismatch: %ld correct, buffer is %ld\n",
	    body_len,ogg_buffer_length(og->body));
    exit(1);
  }

  /* Test data */
  oggbyte_init(&ob,og->body,0);
  for(j=0;j<ogg_buffer_length(og->body);j++)
    if(oggbyte_read1(&ob,j)!=data[j]){
      fprintf(stderr,"body data mismatch (2) at pos %ld: %x!=%x!\n\n",
	      j,data[j],oggbyte_read1(&ob,j));
      exit(1);
    }

  /* Test header */
  oggbyte_init(&ob,og->header,0);
  for(j=0;j<ogg_buffer_length(og->header);j++){
    if(oggbyte_read1(&ob,j)!=header[j]){
      fprintf(stderr,"header content mismatch at pos %ld:\n",j);
      for(j=0;j<header[26]+27;j++)
	fprintf(stderr," (%ld)%02x:%02x",j,header[j],oggbyte_read1(&ob,j));
      fprintf(stderr,"\n");
      exit(1);
    }
  }
  if(ogg_buffer_length(og->header)!=header[26]+27){
    fprintf(stderr,"header length incorrect! (%ld!=%d)\n",
	    ogg_buffer_length(og->header),header[26]+27);
    exit(1);
  }
}

void print_header(ogg_page *og){
  int j;
  oggbyte_buffer ob;
  oggbyte_init(&ob,og->header,0);

  fprintf(stderr,"\nHEADER:\n");
  fprintf(stderr,"  capture: %c %c %c %c  version: %d  flags: %x\n",
	  oggbyte_read1(&ob,0),oggbyte_read1(&ob,1),
	  oggbyte_read1(&ob,2),oggbyte_read1(&ob,3),
	  (int)oggbyte_read1(&ob,4),(int)oggbyte_read1(&ob,5));

  fprintf(stderr,"  granulepos: %08x%08x  serialno: %x  pageno: %ld\n",
	  oggbyte_read4(&ob,10),oggbyte_read4(&ob,6),
	  oggbyte_read4(&ob,14),
	  (long)oggbyte_read4(&ob,18));

  fprintf(stderr,"  checksum: %08x\n  segments: %d (",
	  oggbyte_read4(&ob,22),(int)oggbyte_read1(&ob,26));

  for(j=27;j<ogg_buffer_length(og->header);j++)
    fprintf(stderr,"%d ",(int)oggbyte_read1(&ob,j));
  fprintf(stderr,")\n\n");
}

void error(void){
  fprintf(stderr,"error!\n");
  exit(1);
}

/* 17 only */
const int head1_0[] = {0x4f,0x67,0x67,0x53,0,0x06,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0x15,0xed,0xec,0x91,
		       1,
		       17};

/* 17, 254, 255, 256, 500, 510, 600 byte, pad */
const int head1_1[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0x59,0x10,0x6c,0x2c,
		       1,
		       17};
const int head2_1[] = {0x4f,0x67,0x67,0x53,0,0x04,
		       0x07,0x18,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x89,0x33,0x85,0xce,
		       13,
		       254,255,0,255,1,255,245,255,255,0,
		       255,255,90};

/* nil packets; beginning,middle,end */
const int head1_2[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0xff,0x7b,0x23,0x17,
		       1,
		       0};
const int head2_2[] = {0x4f,0x67,0x67,0x53,0,0x04,
		       0x07,0x28,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x5c,0x3f,0x66,0xcb,
		       17,
		       17,254,255,0,0,255,1,0,255,245,255,255,0,
		       255,255,90,0};

/* large initial packet */
const int head1_3[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0x01,0x27,0x31,0xaa,
		       18,
		       255,255,255,255,255,255,255,255,
		       255,255,255,255,255,255,255,255,255,10};

const int head2_3[] = {0x4f,0x67,0x67,0x53,0,0x04,
		       0x07,0x08,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x7f,0x4e,0x8a,0xd2,
		       4,
		       255,4,255,0};


/* continuing packet test */
const int head1_4[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0xff,0x7b,0x23,0x17,
		       1,
		       0};

const int head2_4[] = {0x4f,0x67,0x67,0x53,0,0x00,
		       0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x34,0x24,0xd5,0x29,
		       17,
		       255,255,255,255,255,255,255,255,
		       255,255,255,255,255,255,255,255,255};

const int head3_4[] = {0x4f,0x67,0x67,0x53,0,0x05,
		       0x07,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,2,0,0,0,
		       0xc8,0xc3,0xcb,0xed,
		       5,
		       10,255,4,255,0};


/* page with the 255 segment limit */
const int head1_5[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0xff,0x7b,0x23,0x17,
		       1,
		       0};

const int head2_5[] = {0x4f,0x67,0x67,0x53,0,0x00,
		       0x07,0xfc,0x03,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0xed,0x2a,0x2e,0xa7,
		       255,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10,10,
		       10,10,10,10,10,10,10};

const int head3_5[] = {0x4f,0x67,0x67,0x53,0,0x04,
		       0x07,0x00,0x04,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,2,0,0,0,
		       0x6c,0x3b,0x82,0x3d,
		       1,
		       50};


/* packet that overspans over an entire page */
const int head1_6[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0xff,0x7b,0x23,0x17,
		       1,
		       0};

const int head2_6[] = {0x4f,0x67,0x67,0x53,0,0x00,
		       0x07,0x04,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x3c,0xd9,0x4d,0x3f,
		       17,
		       100,255,255,255,255,255,255,255,255,
		       255,255,255,255,255,255,255,255};

const int head3_6[] = {0x4f,0x67,0x67,0x53,0,0x01,
		       0x07,0x04,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,2,0,0,0,
		       0xbd,0xd5,0xb5,0x8b,
		       17,
		       255,255,255,255,255,255,255,255,
		       255,255,255,255,255,255,255,255,255};

const int head4_6[] = {0x4f,0x67,0x67,0x53,0,0x05,
		       0x07,0x10,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,3,0,0,0,
		       0xef,0xdd,0x88,0xde,
		       7,
		       255,255,75,255,4,255,0};

/* packet that overspans over an entire page */
const int head1_7[] = {0x4f,0x67,0x67,0x53,0,0x02,
		       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,0,0,0,0,
		       0xff,0x7b,0x23,0x17,
		       1,
		       0};

const int head2_7[] = {0x4f,0x67,0x67,0x53,0,0x00,
		       0x07,0x04,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,1,0,0,0,
		       0x3c,0xd9,0x4d,0x3f,
		       17,
		       100,255,255,255,255,255,255,255,255,
		       255,255,255,255,255,255,255,255};

const int head3_7[] = {0x4f,0x67,0x67,0x53,0,0x05,
		       0x07,0x08,0x00,0x00,0x00,0x00,0x00,0x00,
		       0x01,0x02,0x03,0x04,2,0,0,0,
		       0xd4,0xe0,0x60,0xe5,
		       1,0};

void bufcpy(void *data,ogg_reference *or){
  while(or){
    memcpy(data,or->buffer->data+or->begin,or->length);
    data+=or->length;
    or=or->next;
  }
}

void bufcpy2(void *data,ogg_reference *or,int begin){
  while(or){
    if(or->length-begin>0){
      memcpy(data,or->buffer->data+or->begin+begin,or->length-begin);
      data+=or->length-begin;
    }else
      begin-=or->length;
    or=or->next;
  }
}

int bufcmp(void *data,ogg_reference *or){
  while(or){
    int ret=memcmp(data,or->buffer->data+or->begin,or->length);
    if(ret)return ret;
    data+=or->length;
    or=or->next;
  }
  return 0;
}

void test_pack(const int *pl, const int **headers){
  unsigned char *data=_ogg_malloc(1024*1024); /* for scripted test cases only */
  long inptr=0;
  long outptr=0;
  long deptr=0;
  long depacket=0;
  long granulepos=7,pageno=0;
  int i,j,packets,pageout=0;
  int eosflag=0;
  int bosflag=0;
    
  for(packets=0;;packets++)if(pl[packets]==-1)break;

  for(i=0;i<packets;i++){
    /* construct a test packet */
    ogg_packet op;
    int len=pl[i];
    
    op.packet=ogg_buffer_alloc(bs,len);
    op.e_o_s=(pl[i+1]<0?1:0);
    op.granulepos=granulepos;

    granulepos+=1024;

    for(j=0;j<len;j++)data[inptr+j]=i+j;
    memcpy(op.packet->buffer->data,data+inptr,len);
    op.packet->length=len;

    inptr+=j;
    /* submit the test packet */
    ogg_stream_packetin(os_en,&op);

    /* retrieve any finished pages */
    {
      ogg_page og;
      
      while(ogg_stream_pageout(os_en,&og)){
	/* We have a page.  Check it carefully */

	fprintf(stderr,"%ld, ",pageno);

	if(headers[pageno]==NULL){
	  fprintf(stderr,"coded too many pages!\n");
	  exit(1);
	}

	check_page(data+outptr,headers[pageno],&og);

	outptr+=ogg_buffer_length(og.body);
	pageno++;

	/* have a complete page; submit it to sync/decode */

	{
	  ogg_page og_de;
	  ogg_packet op_de,op_de2;
	  int blen=ogg_buffer_length(og.header)+ogg_buffer_length(og.body);
	  char *buf=ogg_sync_bufferin(oy,blen);
	  bufcpy(buf,og.header);
	  bufcpy(buf+ogg_buffer_length(og.header),og.body);
	  ogg_sync_wrote(oy,blen);

	  while(ogg_sync_pageout(oy,&og_de)>0){
	    /* got a page.  Happy happy.  Verify that it's good. */
	    
	    check_page(data+deptr,headers[pageout],&og_de);
	    deptr+=ogg_buffer_length(og_de.body);
	    pageout++;

	    /* submit it to deconstitution */
	    ogg_stream_pagein(os_de,&og_de);

	    /* packets out? */
	    while(ogg_stream_packetpeek(os_de,NULL)>0){
	      ogg_stream_packetpeek(os_de,&op_de2);
	      ogg_stream_packetout(os_de,&op_de); /* just catching them all */
	      
	      /* verify the packets! */
	      /* check data */
	      if(bufcmp(data+depacket,op_de.packet)){
		fprintf(stderr,"packet data mismatch in decode! pos=%ld\n",
			depacket);
		exit(1);
	      }
	      if(bufcmp(data+depacket,op_de2.packet)){
		fprintf(stderr,"packet data mismatch in peek! pos=%ld\n",
			depacket);
		exit(1);
	      }
	      /* check bos flag */
	      if(bosflag==0 && op_de.b_o_s==0){
		fprintf(stderr,"b_o_s flag not set on packet!\n");
		exit(1);
	      }
	      if(bosflag==0 && op_de2.b_o_s==0){
		fprintf(stderr,"b_o_s flag not set on peek!\n");
		exit(1);
	      }
	      if(bosflag && op_de.b_o_s){
		fprintf(stderr,"b_o_s flag incorrectly set on packet!\n");
		exit(1);
	      }
	      if(bosflag && op_de2.b_o_s){
		fprintf(stderr,"b_o_s flag incorrectly set on peek!\n");
		exit(1);
	      }
	      bosflag=1;
	      depacket+=ogg_buffer_length(op_de.packet);
	      
	      /* check eos flag */
	      if(eosflag){
		fprintf(stderr,"Multiple decoded packets with eos flag!\n");
		exit(1);
	      }

	      if(op_de.e_o_s)eosflag=1;
	      if(op_de.e_o_s!=op_de2.e_o_s){
		fprintf(stderr,"packet/peek eosflag mismatch!\n");
		exit(1);
	      }
	      /* check granulepos flag */
	      if(op_de.granulepos!=-1){
		fprintf(stderr," granule:%ld ",(long)op_de.granulepos);
	      }
	      if(op_de.granulepos!=op_de2.granulepos){
		fprintf(stderr,"packet/peek granpos mismatch!\n");
		exit(1);
	      }

	      ogg_packet_release(&op_de);
	      ogg_packet_release(&op_de2);
	    }
	  }
	}
	ogg_page_release(&og);
      }
    }
  }
  _ogg_free(data);
  if(headers[pageno]!=NULL){
    fprintf(stderr,"did not write last page!\n");
    exit(1);
  }
  if(headers[pageout]!=NULL){
    fprintf(stderr,"did not decode last page!\n");
    exit(1);
  }
  if(inptr!=outptr){
    fprintf(stderr,"encoded page data incomplete!\n");
    exit(1);
  }
  if(inptr!=deptr){
    fprintf(stderr,"decoded page data incomplete!\n");
    exit(1);
  }
  if(inptr!=depacket){
    fprintf(stderr,"decoded packet data incomplete!\n");
    exit(1);
  }
  if(!eosflag){
    fprintf(stderr,"Never got a packet with EOS set!\n");
    exit(1);
  }
  fprintf(stderr,"ok.\n");

  ogg_stream_reset(os_en);
  ogg_stream_reset(os_de);
  ogg_sync_reset(oy);


  ogg_buffer_outstanding(os_en->bufferpool);
  ogg_buffer_outstanding(os_de->bufferpool);
  ogg_buffer_outstanding(oy->bufferpool);
  ogg_buffer_outstanding(bs);

}

int main(void){

  os_en=ogg_stream_create(0x04030201);
  os_de=ogg_stream_create(0x04030201);
  oy=ogg_sync_create();
  bs=ogg_buffer_create();

  /* Exercise each code path in the framing code.  Also verify that
     the checksums are working.  */

  {
    /* 17 only */
    const int packets[]={17, -1};
    const int *headret[]={head1_0,NULL};
    
    fprintf(stderr,"testing single page encoding... ");
    test_pack(packets,headret);
  }

  {
    /* 17, 254, 255, 256, 500, 510, 600 byte, pad */
    const int packets[]={17, 254, 255, 256, 500, 510, 600, -1};
    const int *headret[]={head1_1,head2_1,NULL};
    
    fprintf(stderr,"testing basic page encoding... ");
    test_pack(packets,headret);
  }

  {
    /* nil packets; beginning,middle,end */
    const int packets[]={0,17, 254, 255, 0, 256, 0, 500, 510, 600, 0, -1};
    const int *headret[]={head1_2,head2_2,NULL};
    
    fprintf(stderr,"testing basic nil packets... ");
    test_pack(packets,headret);
  }

  {
    /* large initial packet */
    const int packets[]={4345,259,255,-1};
    const int *headret[]={head1_3,head2_3,NULL};
    
    fprintf(stderr,"testing initial-packet lacing > 4k... ");
    test_pack(packets,headret);
  }

  {
    /* continuing packet test */
    const int packets[]={0,4345,259,255,-1};
    const int *headret[]={head1_4,head2_4,head3_4,NULL};
    
    fprintf(stderr,"testing single packet page span... ");
    test_pack(packets,headret);
  }

  /* page with the 255 segment limit */
  {

    const int packets[]={0,10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,10,
		   10,10,10,10,10,10,10,50,-1};
    const int *headret[]={head1_5,head2_5,head3_5,NULL};
    
    fprintf(stderr,"testing max packet segments... ");
    test_pack(packets,headret);
  }

  {
    /* packet that overspans over an entire page */
    const int packets[]={0,100,9000,259,255,-1};
    const int *headret[]={head1_6,head2_6,head3_6,head4_6,NULL};
    
    fprintf(stderr,"testing very large packets... ");
    test_pack(packets,headret);
  }

  {
    /* term only page.  why not? */
    const int packets[]={0,100,4080,-1};
    const int *headret[]={head1_7,head2_7,head3_7,NULL};
    
    fprintf(stderr,"testing zero data page (1 nil packet)... ");
    test_pack(packets,headret);
  }



  {
    /* build a bunch of pages for testing */
    unsigned char *data=_ogg_malloc(1024*1024);
    int pl[]={0,100,4079,2956,2057,76,34,912,0,234,1000,1000,1000,300,-1};
    int inptr=0,i,j;
    ogg_page og[5];
    
    ogg_stream_reset(os_en);

    for(i=0;pl[i]!=-1;i++){
      ogg_packet op;
      int len=pl[i];

      op.packet=ogg_buffer_alloc(bs,len);
      op.e_o_s=(pl[i+1]<0?1:0);
      op.granulepos=(i+1)*1000;

      for(j=0;j<len;j++)data[inptr+j]=i+j;
      memcpy(op.packet->buffer->data,data+inptr,len);
      op.packet->length=len;

      ogg_stream_packetin(os_en,&op);
      inptr+=j;
    }

    _ogg_free(data);

    /* retrieve finished pages */
    for(i=0;i<5;i++){
      if(ogg_stream_pageout(os_en,&og[i])==0){
	fprintf(stderr,"Too few pages output building sync tests!\n");
	exit(1);
      }
    }
    if(ogg_stream_pageout(os_en,&og[0])>0){
      fprintf(stderr,"Too many pages output building sync tests!\n");
      exit(1);
    }
    
    /* Test lost pages on pagein/packetout: no rollback */
    {
      ogg_page temp;
      ogg_packet test;

      fprintf(stderr,"Testing loss of pages... ");

      ogg_sync_reset(oy);
      ogg_stream_reset(os_de);
      for(i=0;i<5;i++){
	bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[i].header)),
	       og[i].header);
	ogg_sync_wrote(oy,ogg_buffer_length(og[i].header));
	bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[i].body)),
	       og[i].body);
	ogg_sync_wrote(oy,ogg_buffer_length(og[i].body));
      }

      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);
      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);
      ogg_sync_pageout(oy,&temp);
      ogg_page_release(&temp);/* skip */
      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);

      /* do we get the expected results/packets? */
      
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,0,0,0);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,100,1,-1);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,4079,2,3000);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=OGG_HOLE){
	fprintf(stderr,"Error: loss of page did not return error\n");
	exit(1);
      }
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,76,5,-1);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,34,6,-1);
      ogg_packet_release(&test);
      fprintf(stderr,"ok.\n");
    }

    /* Test lost pages on pagein/packetout: rollback with continuation */
    {
      ogg_page temp;
      ogg_packet test;

      fprintf(stderr,"Testing loss of pages (rollback required)... ");

      ogg_sync_reset(oy);
      ogg_stream_reset(os_de);
      for(i=0;i<5;i++){
	bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[i].header)),
	       og[i].header);
	ogg_sync_wrote(oy,ogg_buffer_length(og[i].header));
	bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[i].body)),
	       og[i].body);
	ogg_sync_wrote(oy,ogg_buffer_length(og[i].body));
      }

      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);
      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);
      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);
      ogg_sync_pageout(oy,&temp);
      ogg_page_release(&temp);/* skip */
      ogg_sync_pageout(oy,&temp);
      ogg_stream_pagein(os_de,&temp);

      /* do we get the expected results/packets? */
      
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,0,0,0);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,100,1,-1);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,4079,2,3000);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,2956,3,4000);
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=OGG_HOLE){
	fprintf(stderr,"Error: loss of page did not return error\n");
	exit(1);
      }
      ogg_packet_release(&test);
      if(ogg_stream_packetout(os_de,&test)!=1)error();
      checkpacket(&test,300,13,14000);
      ogg_packet_release(&test);
      fprintf(stderr,"ok.\n");
    }
    
    /* the rest only test sync */
    {
      ogg_page og_de;
      /* Test fractional page inputs: incomplete capture */
      fprintf(stderr,"Testing sync on partial inputs... ");
      ogg_sync_reset(oy);
      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header);
      ogg_sync_wrote(oy,3);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      
      /* Test fractional page inputs: incomplete fixed header */
      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header,3);
      ogg_sync_wrote(oy,20);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      
      /* Test fractional page inputs: incomplete header */
      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header,23);
      ogg_sync_wrote(oy,5);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      
      /* Test fractional page inputs: incomplete body */
      
      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header,28);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].header)-28);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      
      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,1000);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      
      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body,1000);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body)-1000);
      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);
      
      fprintf(stderr,"ok.\n");
    }

    /* Test fractional page inputs: page + incomplete capture */
    {
      ogg_page og_de;
      fprintf(stderr,"Testing sync on 1+partial inputs... ");
      ogg_sync_reset(oy); 

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].header));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header);
      ogg_sync_wrote(oy,20);
      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);

      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header,20);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].header)-20);

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body));

      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);

      fprintf(stderr,"ok.\n");
    }
    
    /* Test recapture: garbage + page */
    {
      ogg_page og_de;
      fprintf(stderr,"Testing search for capture... ");
      ogg_sync_reset(oy); 
      
      /* 'garbage' */
      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].header));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].header)),og[2].header);
      ogg_sync_wrote(oy,20);

      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);
      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);

      bufcpy2(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].header)),og[2].header,20);
      ogg_sync_wrote(oy,ogg_buffer_length(og[2].header)-20);

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].body)),og[2].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[2].body));
      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);

      fprintf(stderr,"ok.\n");
    }

    /* Test recapture: page + garbage + page */
    {
      ogg_page og_de;
      fprintf(stderr,"Testing recapture... ");
      ogg_sync_reset(oy); 

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].header)),og[1].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].header));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[1].body)),og[1].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[1].body));

      /* garbage */

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].header)),og[2].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[2].header));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].header)),og[2].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[2].header));

      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[2].body)),og[2].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[2].body)-5);

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[3].header)),og[3].header);
      ogg_sync_wrote(oy,ogg_buffer_length(og[3].header));

      bufcpy(ogg_sync_bufferin(oy,ogg_buffer_length(og[3].body)),og[3].body);
      ogg_sync_wrote(oy,ogg_buffer_length(og[3].body));

      if(ogg_sync_pageout(oy,&og_de)>0)error();
      ogg_page_release(&og_de);
      if(ogg_sync_pageout(oy,&og_de)<=0)error();
      ogg_page_release(&og_de);

      fprintf(stderr,"ok.\n");
    }
    ogg_stream_destroy(os_en);
    for(i=0;i<5;i++)
      ogg_page_release(&og[i]);
  }

  ogg_stream_destroy(os_de);
  ogg_sync_destroy(oy);

  ogg_buffer_destroy(bs);
  return 0;
}

#endif




