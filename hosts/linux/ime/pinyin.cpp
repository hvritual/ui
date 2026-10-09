#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pinyin.h"
extern "C" {
#include "../package/package.h"
}
#include "pinyinime.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>



namespace {
const size_t dictionary_bytes=1068442;
const char dictionary_digest[]="6bf0bbde4e3134cce38d08524a9f4dc1af40435243c8e36b38eb68d1e14462b2";
struct Request { uint64_t id; PocketTextToken token; uint32_t page,length; char raw[33]; };
struct Worker {
    int socket=-1; pid_t pid=-1;
    bool ready=false,flight=false,queued=false,failed=false;
    uint64_t sequence=0,latest=0,deadline=0,now=0;
    Request next{};
};
void erase(void *p,size_t n){volatile unsigned char *b=static_cast<volatile unsigned char *>(p);while(n--)*b++=0;}
bool token_valid(PocketTextToken t){return t.field_id&&t.session_id&&t.focus_generation&&t.engine_generation;}
bool raw_valid(const char *s,size_t n){
    if(!s||!n||n>POCKET_PINYIN_MAX_INPUT||s[0]=='\''||s[n-1]=='\'')return false;
    for(size_t i=0;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]=='\''&&i&&s[i-1]!='\'')))return false;
    return true;
}
bool digest_valid(const void *data,size_t n){
    if(!data||n!=dictionary_bytes)return false;
    unsigned char digest[32];pui_sha256(data,n,digest);
    const char hex[]="0123456789abcdef";char actual[65]={0};
    for(unsigned i=0;i<32;i++){actual[2*i]=hex[digest[i]>>4];actual[2*i+1]=hex[digest[i]&15];}
    return std::memcmp(actual,dictionary_digest,64)==0;
}
void stop(Worker *w){
    if(w->socket>=0){close(w->socket);w->socket=-1;}
    if(w->pid>0){kill(w->pid,SIGKILL);int st;while(waitpid(w->pid,&st,0)<0&&errno==EINTR){}w->pid=-1;}
    erase(&w->next,sizeof(w->next));w->queued=false;w->flight=false;w->failed=true;
}
bool utf8(const ime_pinyin::char16 *text,char *out,size_t cap,size_t *units){
    size_t j=0;*units=0;
    for(size_t i=0;i<17;i++){
        unsigned c=text[i];if(!c){out[j]=0;*units=i;return i!=0;}
        /* This dictionary emits BMP Han. Reject malformed/unsupported text;
         * never truncate a surrogate or silently replace a candidate. */
        if(c<0x3400||c>0x9fff||j+3>=cap)return false;
        out[j++]=char(0xe0|(c>>12));out[j++]=char(0x80|((c>>6)&63));out[j++]=char(0x80|(c&63));
    }return false;
}
void decode(const Request &q,PocketPinyinResult &r){
    using namespace ime_pinyin;
    r.request=q.id;r.token=q.token;r.page=q.page;r.status=POCKET_PINYIN_INVALID;
    if(q.page>255||!raw_valid(q.raw,q.length))return;
    im_reset_search();size_t total=im_search(q.raw,q.length),decoded=0;
    im_get_sps_str(&decoded);r.decoded_bytes=uint32_t(decoded);r.status=POCKET_PINYIN_OK;
    /* A partial spelling is not a license to discard unparsed input. */
    if(decoded!=q.length)return;
    const uint16 *starts=nullptr;size_t segments=im_get_spl_start_pos(starts);
    if(!starts||segments>16){r.status=POCKET_PINYIN_UNAVAILABLE;return;}
    r.total=uint32_t(total>1280?1280:total);
    size_t first=size_t(q.page)*POCKET_PINYIN_PAGE_SIZE;
    for(size_t n=first;n<r.total&&n<first+POCKET_PINYIN_PAGE_SIZE;n++){
        char16 text[33]={0};size_t units=0;PocketPinyinCandidate c{};
        if(!im_get_candidate(n,text,33)||!utf8(text,c.text,sizeof(c.text),&units)||units>segments)continue;
        c.consumed_bytes=starts[units];
        if(!c.consumed_bytes||c.consumed_bytes>q.length)continue;
        r.candidates[r.count++]=c;
    }
}
[[noreturn]] void child(int fd,int dict_fd,pid_t parent,const int *fds,size_t fd_count){
    for(size_t i=0;i<fd_count;i++)if(fds[i]!=fd&&fds[i]!=dict_fd)close(fds[i]);
    struct rlimit core={0,0};setrlimit(RLIMIT_CORE,&core);
    if(prctl(PR_SET_DUMPABLE,0)||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(1);
    try {
        bool ok=ime_pinyin::im_open_decoder_fd(dict_fd,0,long(dictionary_bytes),nullptr);
        PocketPinyinResult ready{};ready.status=ok&&!ime_pinyin::im_is_user_dictionary_enabled()?POCKET_PINYIN_OK:POCKET_PINYIN_UNAVAILABLE;
        if(send(fd,&ready,sizeof(ready),MSG_NOSIGNAL)!=ssize_t(sizeof(ready))||ready.status!=POCKET_PINYIN_OK)_exit(1);
        ime_pinyin::im_set_max_lens(POCKET_PINYIN_MAX_INPUT,16);
        for(;;){
            Request q{};ssize_t n;
            do{n=recv(fd,&q,sizeof(q),MSG_TRUNC);}while(n<0&&errno==EINTR);
            if(n!=ssize_t(sizeof(q)))break;
            PocketPinyinResult r{};decode(q,r);erase(&q,sizeof(q));
            if(send(fd,&r,sizeof(r),MSG_NOSIGNAL)!=ssize_t(sizeof(r)))break;
            erase(&r,sizeof(r));
        }
        ime_pinyin::im_close_decoder();close(fd);_exit(0);
    }catch(...){_exit(1);}
}
bool drive(Worker *w,uint64_t now){
    if(!w->ready||w->flight||!w->queued)return true;
    ssize_t n=send(w->socket,&w->next,sizeof(w->next),MSG_NOSIGNAL|MSG_DONTWAIT);
    if(n<0&&(errno==EAGAIN||errno==EINTR))return true;
    if(n!=ssize_t(sizeof(w->next))){stop(w);return false;}
    erase(&w->next,sizeof(w->next));w->queued=false;w->flight=true;w->deadline=now+POCKET_PINYIN_TIMEOUT_MS;return true;
}
}
int pocket_pinyin_dictionary_valid(const void *data,size_t bytes){return digest_valid(data,bytes)?1:0;}
void pocket_pinyin_diagnostics(const PocketPinyin *p,int *stage,int *system_errno,unsigned *storage_mode){
    if(stage)*stage=p?p->diagnostic_stage:POCKET_PINYIN_STAGE_NONE;
    if(system_errno)*system_errno=p?p->diagnostic_errno:0;
    if(storage_mode)*storage_mode=p?p->storage_mode:unsigned(POCKET_PINYIN_STORAGE_NONE);
}
static PocketPinyinStatus open_fail(PocketPinyin *out,int stage,int err){
    if(out){out->diagnostic_stage=stage;out->diagnostic_errno=err;}
    return POCKET_PINYIN_UNAVAILABLE;
}
static bool legacy_unavailable(int err){return err==ENOSYS||err==EINVAL||err==EOPNOTSUPP;}
static int write_all(int fd,const void *data,size_t bytes){
    size_t done=0;unsigned interrupts=0;
    while(done<bytes){
        ssize_t n=write(fd,static_cast<const char *>(data)+done,bytes-done);
        if(n<0&&errno==EINTR&&interrupts++<32)continue;
        if(n<=0){if(n==0)errno=EIO;return 0;}
        done+=size_t(n);
    }
    return 1;
}
static int readonly_fallback(PocketPinyin *out,const void *data,size_t bytes){
    char path[]="/tmp/pocket-pinyin-XXXXXX";
    int write_fd=mkstemp(path);
    if(write_fd<0){if(out){out->diagnostic_stage=POCKET_PINYIN_STAGE_TEMPFILE;out->diagnostic_errno=errno;}return -1;}
    (void)fcntl(write_fd,F_SETFD,FD_CLOEXEC);
    if(fchmod(write_fd,S_IRUSR|S_IWUSR)||!write_all(write_fd,data,bytes)){
        int e=errno;close(write_fd);unlink(path);if(out){out->diagnostic_stage=POCKET_PINYIN_STAGE_TEMPFILE;out->diagnostic_errno=e;}return -1;
    }
    int read_fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    int open_errno=read_fd<0?errno:0;
    if(read_fd<0){close(write_fd);unlink(path);if(out){out->diagnostic_stage=POCKET_PINYIN_STAGE_TEMPFILE;out->diagnostic_errno=open_errno;}return -1;}
    struct stat wrote{},readback{};
    int stat_error=(fstat(write_fd,&wrote)||fstat(read_fd,&readback))?errno:0;
    if(stat_error||!S_ISREG(readback.st_mode)||
       wrote.st_dev!=readback.st_dev||wrote.st_ino!=readback.st_ino||
       readback.st_size!=static_cast<off_t>(bytes)||readback.st_nlink!=1){
        int e=stat_error?stat_error:EINVAL;close(read_fd);close(write_fd);unlink(path);
        if(out){out->diagnostic_stage=POCKET_PINYIN_STAGE_TEMPFILE;out->diagnostic_errno=e;}return -1;
    }
    if(unlink(path)){int e=errno;close(read_fd);close(write_fd);if(out){out->diagnostic_stage=POCKET_PINYIN_STAGE_TEMPFILE;out->diagnostic_errno=e;}return -1;}
    close(write_fd);
    out->storage_mode=POCKET_PINYIN_STORAGE_UNLINKED_FILE;
    return read_fd;
}
PocketPinyinStatus pocket_pinyin_open(PocketPinyin *out,const void *data,size_t bytes,uint64_t now){
    if(!out||out->impl||now>UINT64_MAX-POCKET_PINYIN_TIMEOUT_MS)return POCKET_PINYIN_INVALID;
    out->diagnostic_stage=POCKET_PINYIN_STAGE_NONE;out->diagnostic_errno=0;out->storage_mode=POCKET_PINYIN_STORAGE_NONE;
    if(!data||bytes!=dictionary_bytes)return open_fail(out,POCKET_PINYIN_STAGE_DICTIONARY,0);
    int memory=-1;
#ifdef POCKET_PINYIN_TEST_HOOKS
    const char *forced=getenv("POCKET_PINYIN_FORCE_UNLINKED_FILE");
    const char *denied=getenv("POCKET_PINYIN_TEST_MEMFD_DENIED");
    if(denied&&denied[0]=='1'&&!denied[1])errno=EPERM;
    else if(forced&&forced[0]=='1'&&!forced[1])errno=ENOSYS;
    else
#endif
    memory=int(syscall(SYS_memfd_create,"pocket-pinyin",MFD_CLOEXEC|MFD_ALLOW_SEALING));
    if(memory>=0){
        out->storage_mode=POCKET_PINYIN_STORAGE_MEMFD;
        if(!write_all(memory,data,bytes)){int e=errno;close(memory);return open_fail(out,POCKET_PINYIN_STAGE_MEMFD,e);}
        int seal_result;
#ifdef POCKET_PINYIN_TEST_HOOKS
        const char *unsupported=getenv("POCKET_PINYIN_TEST_SEAL_UNSUPPORTED");
        if(unsupported&&unsupported[0]=='1'&&!unsupported[1]){errno=EINVAL;seal_result=-1;}
        else
#endif
        seal_result=fcntl(memory,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL);
        if(seal_result<0){
            int e=errno;close(memory);
            if(!legacy_unavailable(e))return open_fail(out,POCKET_PINYIN_STAGE_SEAL,e);
            memory=readonly_fallback(out,data,bytes);
            if(memory<0)return open_fail(out,out->diagnostic_stage?out->diagnostic_stage:POCKET_PINYIN_STAGE_SEAL,
                                        out->diagnostic_errno?out->diagnostic_errno:e);
        }
    }else{
        int e=errno;
        if(!legacy_unavailable(e))return open_fail(out,POCKET_PINYIN_STAGE_MEMFD,e);
        memory=readonly_fallback(out,data,bytes);
        if(memory<0)return open_fail(out,out->diagnostic_stage?out->diagnostic_stage:POCKET_PINYIN_STAGE_MEMFD,
                                    out->diagnostic_errno?out->diagnostic_errno:e);
    }
    void *view=mmap(nullptr,bytes,PROT_READ,MAP_SHARED,memory,0);
    if(view==MAP_FAILED){int e=errno;close(memory);return open_fail(out,POCKET_PINYIN_STAGE_VERIFY,e);}
    bool valid=digest_valid(view,bytes);munmap(view,bytes);
    if(!valid){close(memory);return open_fail(out,POCKET_PINYIN_STAGE_VERIFY,0);}
    int pair[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)){int e=errno;close(memory);return open_fail(out,POCKET_PINYIN_STAGE_SOCKET,e);}
    /* Enumerate before fork; child closes every inherited device/output fd.
     * Single-owner API is mandatory; this is not a thread-safe fork service. */
    int fds[1024];size_t fd_count=0;bool too_many=false;DIR *dir=opendir("/proc/self/fd");
    if(!dir){int e=errno;close(pair[0]);close(pair[1]);close(memory);return open_fail(out,POCKET_PINYIN_STAGE_FD_ENUM,e);}
    for(dirent *e=readdir(dir);e;e=readdir(dir)){char *end=nullptr;long n=std::strtol(e->d_name,&end,10);if(end&&!*end&&n>=0&&n<=INT_MAX&&n!=dirfd(dir)){if(fd_count==1024){too_many=true;break;}fds[fd_count++]=int(n);}}
    closedir(dir);if(too_many){close(pair[0]);close(pair[1]);close(memory);out->diagnostic_stage=POCKET_PINYIN_STAGE_FD_ENUM;out->diagnostic_errno=EMFILE;return POCKET_PINYIN_EXHAUSTED;}
    Worker *w=new(std::nothrow) Worker;
    if(!w){close(pair[0]);close(pair[1]);close(memory);out->diagnostic_stage=POCKET_PINYIN_STAGE_FORK;out->diagnostic_errno=ENOMEM;return POCKET_PINYIN_EXHAUSTED;}
    pid_t parent=getpid(),pid=fork();
    if(pid==0)child(pair[1],memory,parent,fds,fd_count);
    close(pair[1]);close(memory);
    if(pid<0){int e=errno;close(pair[0]);delete w;return open_fail(out,POCKET_PINYIN_STAGE_FORK,e);}
    w->pid=pid;w->socket=pair[0];w->now=now;w->deadline=now+POCKET_PINYIN_TIMEOUT_MS;out->impl=w;out->diagnostic_stage=POCKET_PINYIN_STAGE_NONE;out->diagnostic_errno=0;return POCKET_PINYIN_OK;
}
PocketPinyinStatus pocket_pinyin_request(PocketPinyin *out,PocketTextToken token,const char *raw,size_t n,uint32_t page,uint64_t now,uint64_t *id){
    Worker *w=out?static_cast<Worker *>(out->impl):nullptr;
    if(id)*id=0;
    if(!w||!id||!token_valid(token)||!raw_valid(raw,n)||page>255||now<w->now||now>UINT64_MAX-POCKET_PINYIN_TIMEOUT_MS)return POCKET_PINYIN_INVALID;
    if(w->failed)return POCKET_PINYIN_UNAVAILABLE;
    if(w->sequence==UINT64_MAX)return POCKET_PINYIN_EXHAUSTED;
    w->now=now;erase(&w->next,sizeof(w->next));w->next.id=++w->sequence;w->latest=w->next.id;w->next.token=token;
    w->next.length=uint32_t(n);w->next.page=page;std::memcpy(w->next.raw,raw,n);w->queued=true;*id=w->latest;
    return drive(w,now)?POCKET_PINYIN_OK:POCKET_PINYIN_UNAVAILABLE;
}
PocketPinyinStatus pocket_pinyin_poll(PocketPinyin *out,uint64_t now,PocketPinyinResult *result){
    Worker *w=out?static_cast<Worker *>(out->impl):nullptr;
    if(result)std::memset(result,0,sizeof(*result));
    if(!w||!result||now<w->now||now>UINT64_MAX-POCKET_PINYIN_TIMEOUT_MS)return POCKET_PINYIN_INVALID;
    w->now=now;if(w->failed)return POCKET_PINYIN_UNAVAILABLE;
    if((!w->ready||w->flight)&&now>=w->deadline){out->diagnostic_stage=w->ready?POCKET_PINYIN_STAGE_SOCKET:POCKET_PINYIN_STAGE_DECODER;out->diagnostic_errno=ETIMEDOUT;stop(w);return POCKET_PINYIN_TIMEOUT;}
    /* Two bounded packets: initial readiness plus one response. */
    for(unsigned i=0;i<2;i++){
        PocketPinyinResult r{};ssize_t n=recv(w->socket,&r,sizeof(r),MSG_DONTWAIT|MSG_TRUNC);
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))break;
        if(n!=ssize_t(sizeof(r))){out->diagnostic_stage=w->ready?POCKET_PINYIN_STAGE_SOCKET:POCKET_PINYIN_STAGE_DECODER;out->diagnostic_errno=n<0?errno:EIO;stop(w);return POCKET_PINYIN_UNAVAILABLE;}
        if(!w->ready){if(r.request||r.status!=POCKET_PINYIN_OK){out->diagnostic_stage=POCKET_PINYIN_STAGE_DECODER;out->diagnostic_errno=0;stop(w);return POCKET_PINYIN_UNAVAILABLE;}w->ready=true;}
        else {w->flight=false;if(r.request==w->latest){*result=r;erase(&r,sizeof(r));return result->status;}}
        erase(&r,sizeof(r));
    }
    if(!drive(w,now))return POCKET_PINYIN_UNAVAILABLE;
    return POCKET_PINYIN_PENDING;
}
void pocket_pinyin_cancel(PocketPinyin *out){
    Worker *w=out?static_cast<Worker *>(out->impl):nullptr;if(!w)return;
    w->latest=0;w->queued=false;erase(&w->next,sizeof(w->next));
}
void pocket_pinyin_close(PocketPinyin *out){
    Worker *w=out?static_cast<Worker *>(out->impl):nullptr;if(!w)return;
    stop(w);delete w;out->impl=nullptr;
}
