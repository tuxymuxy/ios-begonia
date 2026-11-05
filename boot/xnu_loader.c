/* SPDX-License-Identifier: MIT
 * First open-kernel bring-up. Input is ios-sweet's documented SWEETXNU payload
 * format. Only uses the validated upper DDR bank and the reserved log region.
 * No storage, touch, GPU, clocks or power-management initialization.
 */
#include <stdint.h>
#include <stddef.h>
extern char _head[];
extern void xnu_jump(uint64_t entry, uint64_t args);
extern void begonia_emit_text(const char *s);
extern int watcher_start(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t);
static uint64_t arg_number(const char *text,const char *key){
 for(unsigned i=0;i<608;i++){
  unsigned j=0;while(key[j] && i+j<608 && text[i+j]==key[j])j++;
  if(!key[j]){uint64_t n=0;unsigned b=10,k=i+j;if(k+2<608 && text[k]=='0' && text[k+1]=='x'){b=16;k+=2;}
   for(;k<608;k++){char c=text[k];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:99;if(d>=b)break;if(n>(~0ULL-d)/b)return 0;n=n*b+d;}return n;}
 }return 0;
}
struct seg { uint64_t va, size, off, bytes; };
struct payload {
    char magic[8]; uint32_t version, count;
    uint64_t virt, entry, adt_off, adt_size, end, phys, memory;
    char cmdline[608]; struct seg segs[];
};
struct boot_args {
    uint16_t revision, version;
    uint64_t virt, phys, memory, top;
    uint64_t video[6]; uint32_t machine;
    uint64_t tree; uint32_t tree_size;
    char cmdline[608]; uint64_t flags, actual_memory;
};
_Static_assert(offsetof(struct payload,segs)==0x2a8,"payload ABI");
_Static_assert(sizeof(struct boot_args)==736,"boot args ABI");
static void copy(void *to,const void *from,uint64_t n) {
    unsigned char *d=to;const unsigned char *s=from;
    for (uint64_t i=0;i<n;i++) d[i]=s[i];
}
static void zero(void *to,uint64_t n) {
    unsigned char *d=to;for(uint64_t i=0;i<n;i++) d[i]=0;
}
int xnu_main(void) {
    volatile uint32_t *mark=(volatile uint32_t *)0x4d0e0100;
    uint64_t offset=*(uint64_t *)(_head+40), total=*(uint64_t *)(_head+48);
    if(offset<0x10000 || total<0x2a8 || total>0x2000000) return 0;
    const struct payload *p=(const struct payload *)(_head+offset);
    const char *magic="SWEETXNU";
    for(int i=0;i<8;i++)if(p->magic[i]!=magic[i])return 0;
    if(p->version!=1 || !p->count || p->count>32 || 0x2a8+p->count*sizeof(struct seg)>total)return 0;
    if(p->phys!=0x100000000ULL || (p->memory!=0xb7f00000ULL && p->memory!=0xb7e00000ULL) || p->end<p->virt || p->end-p->virt>0x08000000)return 0;
    if(p->entry<p->virt || p->entry>=p->end || p->adt_size>0x100000 || p->adt_off>total || p->adt_size>total-p->adt_off)return 0;
    for(uint32_t i=0;i<p->count;i++) {
        const struct seg *s=&p->segs[i];
        if(s->va<p->virt || s->va-p->virt>0x8000000 || s->size>0x8000000-(s->va-p->virt) || s->bytes>s->size || s->off>total || s->bytes>total-s->off)return 0;
    }
    uint32_t attempt=0x811c9dc5;
    for(unsigned i=0;i<sizeof(p->cmdline);i++)attempt=(attempt^(uint8_t)p->cmdline[i])*0x01000193;
    for(uint64_t i=0;i<p->adt_size;i++)attempt=(attempt^((const uint8_t *)p)[p->adt_off+i])*0x01000193;
    uint32_t request=attempt^0xb00610deu;
    if(!request)request=1;
#ifdef XNU_FORCE_TEST
    volatile uint64_t *saved=(volatile uint64_t *)0x1b7e00000ULL;
    if(saved[0]==0x3230474f4c474542ULL && saved[8]==request){
        begonia_emit_text("BEGONIA XNU: this test log is captured; chaining postmarketOS\n");
        return 0;
    }
    begonia_emit_text("BEGONIA XNU: explicit native test; warm fallback after log capture\n");
    *mark=request;__asm__ volatile("dsb sy" ::: "memory");
#endif
    if(*mark!=request){begonia_emit_text("BEGONIA XNU: no explicit request; chaining postmarketOS\n");return 0;}
    /* Consume before any risky setup. Cold or warm resets default to Linux. */
    *mark=0;__asm__ volatile("dsb sy" ::: "memory");
    begonia_emit_text("BEGONIA XNU: validated payload; copying kernel to upper DDR\n");
    for(uint32_t i=0;i<p->count;i++) {
        const struct seg *s=&p->segs[i];void *dest=(void *)(p->phys+s->va-p->virt);
        copy(dest,(const char *)p+s->off,s->bytes);zero((char *)dest+s->bytes,s->size-s->bytes);
    }
    uint64_t adt=(p->phys+p->end-p->virt+0x3fff)&~0x3fffULL;
    copy((void *)adt,(const char *)p+p->adt_off,p->adt_size);
    uint64_t ba=(adt+p->adt_size+0x3fff)&~0x3fffULL;
    struct boot_args *args=(void *)ba;zero(args,sizeof(*args));
    args->revision=2;args->version=2;args->virt=p->virt;args->phys=p->phys;
    args->memory=p->memory;args->actual_memory=p->memory;args->top=(ba+sizeof(*args)+0x3fff)&~0x3fffULL;
    args->tree=adt-p->phys+p->virt;args->tree_size=(uint32_t)p->adt_size;
    copy(args->cmdline,p->cmdline,sizeof(args->cmdline)-1);
    /* Native file root uses md0 as a dummy and md1 for the small support image.
     * Keep the dummy page outside all kernel/ADT allocations and out of VM. */
    for(unsigned i=0;i<sizeof(args->cmdline)-14;i++) {
        const char *key="begonia_root=1";unsigned j=0;
        while(j<14 && args->cmdline[i+j]==key[j])j++;
        if(j==14){
            if(args->top>p->phys+0x03000000)return 0;
            zero((void *)(p->phys+0x03000000),0x4000);
            args->top=p->phys+0x03004000;
            /* Verified LK layer 1: ARGB8888, 1080x2340, pitch 4320.
             * Use measured registers again and reject a changed scanout. */
            volatile uint32_t *ovl=(volatile uint32_t *)0x14008000;
            uint32_t con=ovl[0x50/4],pitch=ovl[0x64/4]&0xffff,addr=ovl[0xf60/4],dim=ovl[0x58/4];
            if((ovl[0x2c/4]&2) && ((con>>12)&15)==2 && dim==0x09240438 && pitch==4320 && addr>=0x7d9b0000 && (uint64_t)addr+(uint64_t)pitch*2340<=0x7fc00000){
                ovl[0x50/4]=con&~0x100u; /* opaque scanout, as the related MTK loader does */
                __asm__ volatile("dsb sy" ::: "memory");
                args->video[0]=addr;args->video[1]=0;args->video[2]=pitch;args->video[3]=1080;args->video[4]=2340;args->video[5]=32;
                begonia_emit_text("BEGONIA XNU: inherited display accepted\n");
            } else begonia_emit_text("BEGONIA XNU: inherited display changed; headless boot\n");
            break;
        }
    }
    uint64_t lp=arg_number(args->cmdline,"beg_log="),np=arg_number(args->cmdline,"beg_len="),cp=arg_number(args->cmdline,"beg_con=");
    if(lp && (lp<p->phys || lp>p->phys+0x08000000-1536*1024))return 0;
    if(np && (np<p->phys || np>p->phys+0x08000000-4))return 0;
    if(cp && (cp<p->phys || cp>p->phys+0x08000000-8))return 0;
#ifdef QEMU_UART
    uint32_t seconds=5;
#else
    uint64_t configured=arg_number(args->cmdline,"begonia_watch=");
    if(configured && (configured<30 || configured>7200))return 0;
    uint32_t seconds=configured?(uint32_t)configured:240;
#endif
    if(!watcher_start(lp,np,cp,seconds,request)){begonia_emit_text("BEGONIA XNU: supervisor unavailable; returning to Linux\n");return 0;}
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    if(arg_number(args->cmdline,"begonia_root="))begonia_emit_text("BEGONIA XNU: entering kernel with iOS file-backed root\n");
    else begonia_emit_text("BEGONIA XNU: entering open XNU at EL1; no iOS userland\n");
    xnu_jump(p->phys+p->entry-p->virt,ba);
    return 0;
}
