#include "package.h"
#include <string.h>

/* SHA-256, unsigned arithmetic throughout; independent known vectors and
 * Python hashlib cross-checks live in tests/package. Not a crypto certificate. */
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} Hash;
static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32u-n)); }
static uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static void transform(Hash *h, const unsigned char *p) {
    uint32_t w[64], a,b,c,d,e,f,g,z;
    for (unsigned i=0;i<16;i++) w[i]=be32(p+i*4);
    for (unsigned i=16;i<64;i++) {
        uint32_t x=w[i-15], y=w[i-2];
        w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10));
    }
    a=h->state[0];b=h->state[1];c=h->state[2];d=h->state[3];
    e=h->state[4];f=h->state[5];g=h->state[6];z=h->state[7];
    for (unsigned i=0;i<64;i++) {
        uint32_t t=z+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+w[i];
        uint32_t u=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
        z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
    }
    h->state[0]+=a;h->state[1]+=b;h->state[2]+=c;h->state[3]+=d;
    h->state[4]+=e;h->state[5]+=f;h->state[6]+=g;h->state[7]+=z;
}
static void hash_init(Hash *h) {
    *h=(Hash){.state={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
}
static void hash_update(Hash *h, const unsigned char *p, size_t n) {
    h->bytes+=(uint64_t)n;
    while (n) {
        size_t take=64-h->used;if(take>n)take=n;
        memcpy(h->block+h->used,p,take);h->used+=take;p+=take;n-=take;
        if(h->used==64){transform(h,h->block);h->used=0;}
    }
}
static void hash_finish(Hash *h, unsigned char out[32]) {
    uint64_t bits=h->bytes*8;
    h->block[h->used++]=0x80;
    if(h->used>56){memset(h->block+h->used,0,64-h->used);transform(h,h->block);h->used=0;}
    memset(h->block+h->used,0,56-h->used);
    for(unsigned i=0;i<8;i++)h->block[63-i]=(unsigned char)(bits>>(i*8));
    transform(h,h->block);
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[i*4+j]=(unsigned char)(h->state[i]>>(24-j*8));
}
void pui_sha256(const void *p,size_t n,unsigned char out[32]) {
    Hash h;hash_init(&h);hash_update(&h,p,n);hash_finish(&h,out);
}
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t le64(const unsigned char *p) { return le32(p) | (uint64_t)le32(p+4)<<32; }
static int atom(const unsigned char *p,size_t n,int version) {
    size_t end=0;unsigned dots=0,digits=0;
    while(end<n && p[end]) {
        unsigned char c=p[end];
        if(version) {
            if(c=='.'){if(!digits||++dots>2)return 0;digits=0;}
            else if(c>='0'&&c<='9') {if(digits&&p[end-digits]=='0')return 0;++digits;}
            else return 0;
        } else if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return 0;
        end++;
    }
    if(!end||end==n||(version&&(dots!=2||!digits)))return 0;
    if(!version && !(p[0]>='a'&&p[0]<='z'))return 0;
    for(size_t i=end;i<n;i++)if(p[i])return 0;
    return 1;
}
static int file_kind(const unsigned char *p) {
    static const char *const names[]={"application.js","catalog.json","labels.atlas","builtin.rgba",
        "alternate.rgba","Noto-LICENSE.txt","IMAGE-LICENSE.txt",
        "input.atlas","pinyin.dat","PINYIN-NOTICE.txt"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        size_t n=strlen(names[i]);
        if(!memcmp(p,names[i],n)&&p[n]==0){for(size_t j=n;j<48;j++)if(p[j])return -1;return (int)i;}
    }
    return -1;
}
PuiStatus pui_validate(const void *bytes,size_t length,const PuiPolicy *p,PuiPackage *out) {
    PuiPackage result={0};const unsigned char *b=bytes;unsigned char digest[32];
    if(out)memset(out,0,sizeof(*out));
    if(!b||!p||!out||(p->target!=PUI_TARGET_600&&p->target!=PUI_TARGET_800))return PUI_ARGUMENT;
    if(length<PUI_HEADER_SIZE)return PUI_TRUNCATED;
    if(length>PUI_MAX_BYTES)return PUI_BUDGET;
    if(memcmp(b,"PUI1\r\n\032\n",8)||le32(b+12)!=PUI_HEADER_SIZE||le32(b+60))return PUI_FORMAT;
    if(le32(b+8)!=PUI_FORMAT_VERSION)return PUI_VERSION;
    result.runtime_min=le32(b+16);result.runtime_max=le32(b+20);result.sdk_api=le32(b+24);
    if(!result.runtime_min||result.runtime_min>result.runtime_max||p->runtime_api<result.runtime_min||
       p->runtime_api>result.runtime_max||result.sdk_api!=p->sdk_api)return PUI_VERSION;
    result.targets=le32(b+28);result.capabilities=le32(b+32);result.file_count=le32(b+36);
    if(!result.targets||(result.targets&~3u)||!(result.targets&p->target))return PUI_TARGET;
    if(!(result.capabilities&PUI_CAP_CORE)||(result.capabilities&~PUI_CAP_ALL)||
       (result.capabilities&~p->capabilities))return PUI_CAPABILITY;
    uint64_t payload=le64(b+40);result.heap_bytes=le32(b+48);result.asset_bytes=le32(b+52);
    if(result.heap_bytes<1024u*1024u||result.heap_bytes>PUI_MAX_HEAP||result.heap_bytes>p->max_heap_bytes||
       !result.asset_bytes||result.asset_bytes>PUI_MAX_BYTES||result.asset_bytes>p->max_asset_bytes||
       payload>result.asset_bytes||le32(b+56)!=256u*1024u)return PUI_BUDGET;
    if(!p->allow_unsigned)return PUI_UNSIGNED;
    if(!atom(b+64,64,0)||!atom(b+128,32,1))return PUI_FORMAT;
    if(!result.file_count||result.file_count>PUI_MAX_FILES)return PUI_FILE_TABLE;
    size_t table=(size_t)result.file_count*PUI_ENTRY_SIZE;
    if(table>length-PUI_HEADER_SIZE||payload!=length-PUI_HEADER_SIZE-table)return PUI_TRUNCATED;
    Hash hash;hash_init(&hash);hash_update(&hash,b,160);hash_update(&hash,b+PUI_HEADER_SIZE,length-PUI_HEADER_SIZE);hash_finish(&hash,digest);
    if(memcmp(digest,b+160,32))return PUI_INTEGRITY;
    memcpy(result.app_id,b+64,64);memcpy(result.app_version,b+128,32);
    size_t cursor=0;unsigned found=0;
    for(unsigned i=0;i<result.file_count;i++) {
        const unsigned char *entry=b+PUI_HEADER_SIZE+i*PUI_ENTRY_SIZE;
        int kind=file_kind(entry);if(kind<0)return PUI_FILE_TABLE;
        if(i&&strcmp(result.files[i-1].name,(const char *)entry)>=0)return PUI_FILE_TABLE;
        uint64_t offset=le64(entry+48),size=le64(entry+56);
        if(!size||offset!=cursor||size>payload-cursor)return PUI_FILE_TABLE;
        if((kind==0&&size>256u*1024u)||(kind==1&&size>64u*1024u))return PUI_BUDGET;
        const unsigned char *data=b+PUI_HEADER_SIZE+table+cursor;
        pui_sha256(data,(size_t)size,digest);if(memcmp(digest,entry+64,32))return PUI_INTEGRITY;
        memcpy(result.files[i].name,entry,48);result.files[i].data=data;result.files[i].length=(size_t)size;
        cursor+=(size_t)size;found|=1u<<(unsigned)kind;
    }
    if(cursor!=payload)return PUI_FILE_TABLE;
    if((found&15u)!=15u)return PUI_REQUIRED_FILE;
    const unsigned input_files=(1u<<7)|(1u<<8)|(1u<<9);
    if(result.capabilities&PUI_CAP_PINYIN){
        if(!(result.capabilities&PUI_CAP_ASCII_KEYBOARD))return PUI_CAPABILITY;
        if((found&input_files)!=input_files)return PUI_REQUIRED_FILE;
    }else if(found&input_files)return PUI_CAPABILITY;
    *out=result;return PUI_OK;
}
const char *pui_status_name(PuiStatus s) {
    static const char *const names[]={"PUI_OK","PUI_ARGUMENT","PUI_TRUNCATED","PUI_FORMAT","PUI_VERSION",
        "PUI_TARGET","PUI_CAPABILITY","PUI_BUDGET","PUI_UNSIGNED","PUI_INTEGRITY","PUI_FILE_TABLE","PUI_REQUIRED_FILE"};
    return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"PUI_UNKNOWN";
}
