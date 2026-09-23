/* Independent assertions around the real Core's latest damage result. The
 * callback represents a framebuffer destination; no drawing is faked. */
static int quality_present_count;
static int quality_present_fail;
static int quality_present(void *context,const HostFrame *frame) {
 (void)context;
 if(!frame||!frame->pixels||!frame->length||quality_present_fail)return 0;
 ++quality_present_count;return 1;
}
static int render_quality_suite(const char *assets,unsigned height) {
 LinuxHost host={0};HostFrame frame;HostAsset packet={0};int ok=0;
 const char *profile=height==600?"imx6ul-1024x600":"imx6ul-1024x800";
 #define QUALITY_CHECK(x) do{if(!(x)){fprintf(stderr,"RENDER_QUALITY_FAIL line=%d %s\n",__LINE__,#x);goto done;}}while(0)
 quality_present_count=quality_present_fail=0;
 QUALITY_CHECK(host_open(&host,profile,assets,"coffee.js","labels.atlas",0));
 QUALITY_CHECK(pocket_runtime_harness_bind("inspect"));
 QUALITY_CHECK(media_builtin(assets));
 QUALITY_CHECK(host_render(&host,&frame));
 QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==1&&host.presented_frames==1);
 // Core text raster must retain intermediate coverage on the opaque BGRA
 // destination. This bounds the title region, excluding cards and images.
 unsigned coverage=0;
 for(unsigned y=20;y<52;y++)for(unsigned x=32;x<600;x++) {
  const unsigned char *p=frame.pixels+y*frame.stride+x*4;
  if(p[2]>48&&p[2]<248&&p[1]>55&&p[1]<246&&p[0]>52&&p[0]<240)coverage++;
 }
 QUALITY_CHECK(coverage>100);
 for(int i=0;i<120;i++) {
  QUALITY_CHECK(host_turn_contacts(&host,NULL));
  if(host.turns%2==0) {
   QUALITY_CHECK(host_render(&host,&frame));
   QUALITY_CHECK(pocket_runtime_damage_pixels()==0);
   QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
  }
 }
 QUALITY_CHECK(host.turns==120&&host.clean_frames_skipped==60&&quality_present_count==1);
 QUALITY_CHECK(host_present_latest(&host,&frame,1,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==2);
 // A real language-button gesture invalidates the retained Core image.
 QUALITY_CHECK(tap(&host,890,45));
 QUALITY_CHECK(host_render(&host,&frame));
 QUALITY_CHECK(pocket_runtime_damage_pixels()>0);
 QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==3);
 // Media event between guest turns must be visible in the next presentation.
 QUALITY_CHECK(host_asset_read(assets,"alternate.rgba",MEDIA_PACKET_BYTES,&packet));
 QUALITY_CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==1);
 QUALITY_CHECK(host_render(&host,&frame));
 QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==4);
 // Rejected packets must neither replace the old texture nor dirty the scene.
 packet.data[0]^=1;
 QUALITY_CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==-1);
 QUALITY_CHECK(host_render(&host,&frame));
 QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==4);
 // Recovery/exposure invalidates destination even though Core stays clean.
 QUALITY_CHECK(host_pause(&host,1,1000000000ULL));
 QUALITY_CHECK(host_pause(&host,0,1000000001ULL));
 QUALITY_CHECK(host_render(&host,&frame));
 QUALITY_CHECK(host_present_latest(&host,&frame,0,quality_present,NULL));
 QUALITY_CHECK(quality_present_count==5);
 quality_present_fail=1;
 QUALITY_CHECK(!host_present_latest(&host,&frame,1,quality_present,NULL));
 QUALITY_CHECK(host.state==HOST_FAILED&&quality_present_count==5);
 ok=1;
 done:host_close(&host);host_asset_free(&packet);quality_present_fail=0;
 #undef QUALITY_CHECK
 if(ok)printf("PASS render-quality-%u clean-skips=60 changed-forced-recovery=true text-coverage=true\n",height);
 return ok;
}
