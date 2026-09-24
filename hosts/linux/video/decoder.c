/* Standalone FFmpeg adapter. The UI links no FFmpeg library. Source and
 * relinkable objects are included in the decoder redistribution kit. */
#define _POSIX_C_SOURCE 200809L
#include "wire.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

typedef struct {
 int fd;
 uint64_t session,seq,last_pts,pending_decode_us;
 int64_t first_pts;
 unsigned width,height,fpsn,fpsd;
 AVFormatContext *format;
 AVCodecContext *codec;
 AVIOContext *io;
 AVFrame *frame;
 AVPacket *packet;
 struct SwsContext *scale;
 uint8_t *pixels;
 int stream;
 const char *error;
} Decoder;
static uint64_t micros(void) { struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
static int number(const char *s,uint64_t max,uint64_t *out) {
 if(!s||!*s)return 0;
 for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return 0;
 errno=0;char *end;unsigned long long v=strtoull(s,&end,10);if(errno||*end||v>max)return 0;*out=v;return 1;
}
static int read_file(void *context,uint8_t *dst,int size) {
 Decoder *d=context;ssize_t n=read(d->fd,dst,(size_t)size);
 return n>0?(int)n:n==0?AVERROR_EOF:AVERROR(errno);
}
static int64_t seek_file(void *context,int64_t offset,int whence) {
 Decoder *d=context;
 if(whence==AVSEEK_SIZE){struct stat st;if(fstat(d->fd,&st))return AVERROR(errno);return st.st_size;}
 if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END)return AVERROR(EINVAL);
 off_t n=lseek(d->fd,(off_t)offset,whence);return n<0?AVERROR(errno):(int64_t)n;
}
static int deny_open(AVFormatContext *f,AVIOContext **pb,const char *url,int flags,AVDictionary **options) {
 (void)f;(void)pb;(void)url;(void)flags;(void)options;return AVERROR(EPERM);
}
static int output(const void *data,size_t n) {
 const uint8_t *p=data;
 while(n){ssize_t v=write(STDOUT_FILENO,p,n);if(v<0&&errno==EINTR)continue;if(v<=0)return 0;p+=v;n-=(size_t)v;}
 return 1;
}
static int emit(Decoder *d,int eof,uint64_t decode_us) {
 uint8_t h[VIDEO_WIRE_BYTES]={0};memcpy(h,"PUIVFR1\0",8);video_put32(h+8,eof?2:1);
 video_put32(h+12,d->width);video_put32(h+16,d->height);video_put32(h+20,d->width*4);
 video_put32(h+24,eof?0:d->width*d->height*4);video_put32(h+28,1);
 video_put64(h+32,d->session);video_put64(h+40,d->seq);video_put64(h+48,d->last_pts);
 video_put32(h+56,(uint32_t)(1000000ULL*d->fpsd/d->fpsn));video_put32(h+60,decode_us>UINT32_MAX?UINT32_MAX:(uint32_t)decode_us);
 return output(h,sizeof(h))&&(eof||output(d->pixels,(size_t)d->width*d->height*4));
}
static int frames(Decoder *d) {
 for(;;) {
  uint64_t start=micros();int r=avcodec_receive_frame(d->codec,d->frame);
  if(r==AVERROR(EAGAIN)||r==AVERROR_EOF)return 1;
  if(r<0){d->error="decode";return 0;}
  AVFrame *f=d->frame;
  if(f->width!=(int)d->width||f->height!=(int)d->height||f->format!=AV_PIX_FMT_YUV420P||
     (f->flags&AV_FRAME_FLAG_INTERLACED)||f->best_effort_timestamp==AV_NOPTS_VALUE||
     f->color_trc==AVCOL_TRC_SMPTE2084||f->color_trc==AVCOL_TRC_ARIB_STD_B67){d->error="frame-format-or-pts";return 0;}
  int64_t pts=av_rescale_q(f->best_effort_timestamp,d->format->streams[d->stream]->time_base,AV_TIME_BASE_Q);
  if(d->first_pts==INT64_MIN)d->first_pts=pts;
  if(pts<d->first_pts||d->first_pts>INT64_MAX-(int64_t)VIDEO_DURATION_US||pts>d->first_pts+(int64_t)VIDEO_DURATION_US||
     (d->seq&&(uint64_t)(pts-d->first_pts)<=d->last_pts)||d->seq>=VIDEO_FRAME_LIMIT){d->error="frame-time-budget";return 0;}
  d->last_pts=(uint64_t)(pts-d->first_pts);d->seq++;
  if(!d->scale)d->scale=sws_getContext((int)d->width,(int)d->height,AV_PIX_FMT_YUV420P,(int)d->width,(int)d->height,AV_PIX_FMT_BGRA,SWS_BILINEAR|SWS_ACCURATE_RND|SWS_BITEXACT,NULL,NULL,NULL);
  if(!d->scale){d->error="color-converter";return 0;}
  int matrix=f->colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:SWS_CS_ITU601;
  if(sws_setColorspaceDetails(d->scale,sws_getCoefficients(matrix),f->color_range==AVCOL_RANGE_JPEG,sws_getCoefficients(matrix),1,0,1<<16,1<<16)<0){d->error="color-matrix";return 0;}
  uint8_t *dst[4]={d->pixels,NULL,NULL,NULL};int lines[4]={(int)d->width*4,0,0,0};
  if(sws_scale(d->scale,(const uint8_t *const *)f->data,f->linesize,0,(int)d->height,dst,lines)!=(int)d->height){d->error="color-output";return 0;}
  av_frame_unref(f);
  uint64_t work=d->pending_decode_us+micros()-start;d->pending_decode_us=0;
  if(!emit(d,0,work)){d->error="output";return 0;}
 }
}
static int decode(Decoder *d) {
 struct stat st;
 if(fstat(d->fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>VIDEO_FILE_BYTES||lseek(d->fd,0,SEEK_SET)<0){d->error="input-file";return 0;}
 d->format=avformat_alloc_context();if(!d->format){d->error="allocation";return 0;}
 uint8_t *buffer=av_malloc(32768);if(!buffer){d->error="allocation";return 0;}
 d->io=avio_alloc_context(buffer,32768,0,d,read_file,NULL,seek_file);
 if(!d->io){av_free(buffer);d->error="allocation";return 0;}
 d->format->pb=d->io;d->format->flags|=AVFMT_FLAG_CUSTOM_IO;d->format->io_open=deny_open;
 d->format->max_streams=4;d->format->probesize=1024*1024;d->format->max_analyze_duration=2*AV_TIME_BASE;
 const AVInputFormat *mov=av_find_input_format("mov");
 if(!mov||avformat_open_input(&d->format,NULL,mov,NULL)<0||avformat_find_stream_info(d->format,NULL)<0){d->error="mp4-demux";return 0;}
 d->stream=-1;
 for(unsigned i=0;i<d->format->nb_streams;i++) {
  if(d->format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){if(d->stream>=0){d->error="multiple-video-streams";return 0;}d->stream=(int)i;}
 }
 if(d->stream<0){d->error="no-video";return 0;}
 AVStream *stream=d->format->streams[d->stream];AVCodecParameters *p=stream->codecpar;
 AVRational rate=av_guess_frame_rate(d->format,stream,NULL);
 for(int i=0;i<p->nb_coded_side_data;i++)
  if(p->coded_side_data[i].type==AV_PKT_DATA_DISPLAYMATRIX){d->error="display-matrix-not-admitted";return 0;}
 if(p->codec_id!=AV_CODEC_ID_H264||p->width!=(int)d->width||p->height!=(int)d->height||
    p->extradata_size>256*1024||rate.num<=0||rate.den<=0||rate.num>(int64_t)30*rate.den||
    (int64_t)rate.num*d->fpsd!=(int64_t)d->fpsn*rate.den||
    (p->sample_aspect_ratio.num>0&&p->sample_aspect_ratio.den>0&&p->sample_aspect_ratio.num!=p->sample_aspect_ratio.den)){
  d->error="codec-dimensions-fps-or-sar";return 0;
 }
 const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_H264);d->codec=avcodec_alloc_context3(codec);
 if(!codec||!d->codec||avcodec_parameters_to_context(d->codec,p)<0){d->error="codec-allocation";return 0;}
 d->codec->max_pixels=VIDEO_WIDTH_MAX*VIDEO_HEIGHT_MAX;d->codec->thread_count=1;d->codec->thread_type=0;
 d->codec->err_recognition=AV_EF_EXPLODE|AV_EF_BITSTREAM|AV_EF_BUFFER;
 if(avcodec_open2(d->codec,codec,NULL)<0){d->error="codec-open";return 0;}
 d->frame=av_frame_alloc();d->packet=av_packet_alloc();d->pixels=av_malloc(VIDEO_FRAME_BYTES);
 if(!d->frame||!d->packet||!d->pixels){d->error="allocation";return 0;}
 int r;
 while((r=av_read_frame(d->format,d->packet))>=0) {
  if(d->packet->stream_index==d->stream) {
   uint64_t before=micros();int sent=avcodec_send_packet(d->codec,d->packet);d->pending_decode_us+=micros()-before;
   if(sent==AVERROR(EAGAIN)){if(!frames(d))return 0;before=micros();sent=avcodec_send_packet(d->codec,d->packet);d->pending_decode_us+=micros()-before;}
   if(sent<0){d->error="packet";return 0;}
   if(!frames(d))return 0;
  }
  av_packet_unref(d->packet);
 }
 if(r!=AVERROR_EOF){d->error="truncated-input";return 0;}
 uint64_t flush_start=micros();int flushed=avcodec_send_packet(d->codec,NULL);
 d->pending_decode_us+=micros()-flush_start;
 if(flushed<0||!frames(d)||d->seq==0)return 0;
 return emit(d,1,0);
}
int main(int argc,char **argv) {
 uint64_t v[6];
 if(argc!=7){fprintf(stderr,"VIDEO_DECODER_ARGUMENTS\n");return 2;}
 for(int i=0;i<6;i++)if(!number(argv[i+1],i==1?UINT64_MAX:30000,&v[i]))return 2;
 if(v[0]>1024||!v[1]||!video_dimensions((unsigned)v[2],(unsigned)v[3])||!v[4]||!v[5]||v[5]>1001||v[4]>30*v[5])return 2;
 Decoder d={.fd=(int)v[0],.session=v[1],.width=(unsigned)v[2],.height=(unsigned)v[3],.fpsn=(unsigned)v[4],.fpsd=(unsigned)v[5],.first_pts=INT64_MIN};
 av_log_set_level(AV_LOG_QUIET);av_max_alloc(32U*1024U*1024U);
 int ok=decode(&d);
 if(d.scale)sws_freeContext(d.scale);
 av_free(d.pixels);av_packet_free(&d.packet);av_frame_free(&d.frame);avcodec_free_context(&d.codec);
 avformat_close_input(&d.format);
 if(d.io){av_freep(&d.io->buffer);avio_context_free(&d.io);}
 close(d.fd);
 fprintf(stderr,"VIDEO_DECODER_%s frames=%llu reason=%s\n",ok?"OK":"REJECTED",(unsigned long long)d.seq,d.error?d.error:"none");
 return ok?0:1;
}
