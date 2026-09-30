#ifndef TEST_REGION_CASES_H
#define TEST_REGION_CASES_H
/* Per-byte oracle, with destination padding and hidden rows initialized to A5. */
static int region_cases(void){
    uint8_t pixels[3*271],expected[4096],before[4096];
    for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(uint8_t)(i*17U+3U);
    HostFrame f={pixels,67,3,271,sizeof(pixels)};FbDevice device={0};int ok=0;
#define REGION_CHECK(x) do{if(!(x)){fprintf(stderr,"REGION_FAILED line=%d %s\n",__LINE__,#x);goto done;}}while(0)
    const FbDamage regions[]={{1,1,65,1},{66,2,1,1},{0,0,67,3}};
    for(unsigned format=0;format<4;format++)for(unsigned j=0;j<3;j++){
        fake_fb_reset(67,3,format==0?16:32);
        if(format==2){fake_fb.var.red.offset=0;fake_fb.var.blue.offset=16;}
        if(format==3)fake_fb.var.transp=(struct fb_bitfield){24,8,0};
        REGION_CHECK(fake_fb.fix.smem_len<=sizeof(expected));memset(expected,0xa5,sizeof(expected));
        const FbDamage *r=&regions[j];
        for(int y=r->y;y<r->y+r->height;y++)for(int x=r->x;x<r->x+r->width;x++){
            unsigned bpp=fake_fb.var.bits_per_pixel/8;
            size_t d=((size_t)y+fake_fb.var.yoffset)*fake_fb.fix.line_length+((size_t)x+fake_fb.var.xoffset)*bpp;
            const uint8_t *p=pixels+(size_t)y*f.stride+(size_t)x*4;
            if(bpp==2){uint16_t q=(uint16_t)((p[2]>>3)*2048+(p[1]>>2)*32+(p[0]>>3));expected[d]=(uint8_t)q;expected[d+1]=(uint8_t)(q>>8);}
            else{memset(expected+d,0,4);expected[d+fake_fb.var.red.offset/8]=p[2];expected[d+fake_fb.var.green.offset/8]=p[1];expected[d+fake_fb.var.blue.offset/8]=p[0];if(fake_fb.var.transp.length)expected[d+3]=255;}
        }
        REGION_CHECK(!fb_copy_region(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&f,r));
        REGION_CHECK(!memcmp(expected,fake_fb.data,fake_fb.fix.smem_len)&&fake_fb_guards());
    }
    puts("PASS damage-copy-formats-padding-offset-and-block-tail");
    fake_fb_reset(67,3,32);
    const FbDamage invalid[]={{-1,0,1,1},{0,-1,1,1},{0,0,0,1},{0,0,1,0},{66,0,2,1},{0,2,1,2},{INT_MAX,0,INT_MAX,1}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
        REGION_CHECK(fb_copy_region(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&f,&invalid[i]));
        REGION_CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len));
    }
    f.length=10;REGION_CHECK(fb_copy_region(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&f,&regions[0]));f.length=sizeof(pixels);
    REGION_CHECK(fb_copy_region(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len-1,&f,&regions[0]));
    f.pixels=fake_fb.data;REGION_CHECK(fb_copy_region(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&f,&regions[0]));f.pixels=pixels;
    REGION_CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len));puts("PASS damage-invalid-source-map-alias-and-rectangle-no-writes");
    REGION_CHECK(fbdev_open(&device,"/dev/fb-fixture",1));
    REGION_CHECK(!fbdev_present_region(&device,&f,&regions[0]));
    REGION_CHECK(!strcmp(device.error,"FB_DAMAGE_WITHOUT_BASELINE")&&unchanged(fake_fb.data,fake_fb.fix.smem_len));
    REGION_CHECK(fbdev_close(&device));puts("PASS damage-requires-initial-full-frame");
    fake_fb_reset(67,3,32);REGION_CHECK(fbdev_open(&device,"/dev/fb-fixture",1));
    REGION_CHECK(fbdev_present(&device,&f));REGION_CHECK(fbdev_present_region(&device,&f,&regions[0]));
    REGION_CHECK(device.presents==2&&device.partial_presents==1&&device.bytes_written==(67U*3+65)*4);
    REGION_CHECK(fbdev_close(&device));puts("PASS damage-exact-byte-accounting");
    fake_fb_reset(67,3,32);REGION_CHECK(fbdev_open(&device,"/dev/fb-fixture",1));REGION_CHECK(fbdev_present(&device,&f));
    memcpy(before,fake_fb.data,fake_fb.fix.smem_len);fake_fb.var.xoffset++;
    REGION_CHECK(!fbdev_present_region(&device,&f,&regions[0])&&!strcmp(device.error,"FB_MODE_CHANGED"));
    REGION_CHECK(!memcmp(before,fake_fb.data,fake_fb.fix.smem_len));REGION_CHECK(fbdev_close(&device));
    puts("PASS damage-revalidates-mode-before-write");ok=1;
done:
    (void)fbdev_close(&device);
#undef REGION_CHECK
    return ok;
}
#endif
