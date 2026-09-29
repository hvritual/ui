#include "layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define LAYOUT_DEFAULT_RECORDS 1024U

typedef struct {
    PocketUiHandle node;
    PocketLayoutSpec spec;
    PocketLayoutResult result;
    int used;
    int has_result;
} LayoutRecord;

typedef struct {
    PocketUiTree *tree;
    PocketLayoutInsets safe;
    PocketLayoutMeasureFn measure;
    void *measure_context;
    LayoutRecord *records;
    uint32_t capacity;
} LayoutImpl;

typedef struct {
    PocketUiHandle node;
    PocketLayoutSpec spec;
    int32_t main, cross;
    int32_t before_main, after_main, before_cross, after_cross;
    uint32_t flex_grow, flex_shrink;
    int32_t min_main, max_main, min_cross, max_cross;
} FlexChild;

static LayoutImpl *li(PocketLayoutContext *context) {
    return context ? (LayoutImpl *)context->impl : NULL;
}
static const LayoutImpl *cli(const PocketLayoutContext *context) {
    return context ? (const LayoutImpl *)context->impl : NULL;
}
static int handle_equal(PocketUiHandle a, PocketUiHandle b) {
    return a.slot == b.slot && a.generation == b.generation;
}
static int insets_valid(PocketLayoutInsets v) {
    return v.left >= 0 && v.top >= 0 && v.right >= 0 && v.bottom >= 0;
}
static int length_valid(PocketLength v, int offset) {
    if(v.kind < POCKET_LENGTH_AUTO || v.kind > POCKET_LENGTH_FILL) return 0;
    if(offset && v.kind != POCKET_LENGTH_PX && v.kind != POCKET_LENGTH_PERCENT) return 0;
    if(v.kind == POCKET_LENGTH_PERCENT) return v.value >= 0 && v.value <= 10000;
    if(v.kind == POCKET_LENGTH_PX) return offset || v.value >= 0;
    return v.value == 0;
}
static int spec_valid(const PocketLayoutSpec *s) {
    if(!s || s->mode < POCKET_LAYOUT_LEAF || s->mode > POCKET_LAYOUT_ABSOLUTE ||
       !length_valid(s->width,0) || !length_valid(s->height,0) ||
       !length_valid(s->offset_x,1) || !length_valid(s->offset_y,1) ||
       s->min_width < 0 || s->min_height < 0 ||
       s->max_width < -1 || s->max_height < -1 ||
       (s->max_width >= 0 && s->max_width < s->min_width) ||
       (s->max_height >= 0 && s->max_height < s->min_height) ||
       !insets_valid(s->margin) || !insets_valid(s->padding) || s->gap < 0 ||
       s->align_items < POCKET_ALIGN_AUTO || s->align_items > POCKET_ALIGN_STRETCH ||
       s->align_self < POCKET_ALIGN_AUTO || s->align_self > POCKET_ALIGN_STRETCH ||
       s->justify_content < POCKET_JUSTIFY_START ||
       s->justify_content > POCKET_JUSTIFY_SPACE_BETWEEN ||
       s->grid_columns > 16U || s->overflow > POCKET_OVERFLOW_CLIP ||
       s->wrap > 1U) return 0;
    if((s->aspect_num == 0U) != (s->aspect_den == 0U)) return 0;
    if(s->aspect_num > 4096U || s->aspect_den > 4096U) return 0;
    if(s->mode == POCKET_LAYOUT_GRID && s->grid_columns == 0U) return 0;
    return 1;
}
PocketLayoutSpec pocket_layout_spec_default(void) {
    PocketLayoutSpec s;
    memset(&s,0,sizeof(s));
    s.mode = POCKET_LAYOUT_LEAF;
    s.width.kind = s.height.kind = POCKET_LENGTH_AUTO;
    s.offset_x.kind = s.offset_y.kind = POCKET_LENGTH_PX;
    s.max_width = s.max_height = -1;
    s.shrink = 1;
    s.align_items = POCKET_ALIGN_START;
    s.align_self = POCKET_ALIGN_AUTO;
    s.justify_content = POCKET_JUSTIFY_START;
    s.grid_columns = 1;
    s.overflow = POCKET_OVERFLOW_VISIBLE;
    return s;
}
static LayoutRecord *find_record(LayoutImpl *impl, PocketUiHandle node, int create) {
    LayoutRecord *free_record = NULL;
    if(!impl || !pocket_ui_handle_valid(node)) return NULL;
    for(uint32_t i=0;i<impl->capacity;i++) {
        LayoutRecord *r=&impl->records[i];
        if(r->used && handle_equal(r->node,node)) return r;
        if(r->used) {
            PocketUiSnapshot snapshot;
            PocketUiStatus status=pocket_ui_snapshot(impl->tree,r->node,&snapshot);
            if(status==POCKET_UI_STALE_HANDLE) {
                memset(r,0,sizeof(*r));
            }
        }
        if(!r->used && !free_record) free_record=r;
    }
    if(!create || !free_record) return NULL;
    free_record->used=1;
    free_record->node=node;
    free_record->spec=pocket_layout_spec_default();
    return free_record;
}
static const LayoutRecord *find_record_const(const LayoutImpl *impl, PocketUiHandle node) {
    if(!impl || !pocket_ui_handle_valid(node)) return NULL;
    for(uint32_t i=0;i<impl->capacity;i++) {
        const LayoutRecord *r=&impl->records[i];
        if(r->used && handle_equal(r->node,node)) return r;
    }
    return NULL;
}
PocketUiStatus pocket_layout_init(PocketLayoutContext *context, const PocketLayoutConfig *config) {
    if(!context || context->impl || !config || !config->tree || !insets_valid(config->safe_area))
        return POCKET_UI_INVALID_ARGUMENT;
    uint32_t capacity=config->record_capacity?config->record_capacity:LAYOUT_DEFAULT_RECORDS;
    if(!capacity || capacity > 65535U) return POCKET_UI_INVALID_ARGUMENT;
    LayoutImpl *impl=calloc(1,sizeof(*impl));
    if(!impl) return POCKET_UI_RESOURCE_EXHAUSTED;
    impl->records=calloc(capacity,sizeof(*impl->records));
    if(!impl->records){free(impl);return POCKET_UI_RESOURCE_EXHAUSTED;}
    impl->tree=config->tree; impl->safe=config->safe_area; impl->measure=config->measure;
    impl->measure_context=config->measure_context; impl->capacity=capacity;
    context->impl=impl;
    return POCKET_UI_OK;
}
void pocket_layout_dispose(PocketLayoutContext *context) {
    LayoutImpl *impl=li(context);
    if(!impl)return;
    free(impl->records); free(impl); context->impl=NULL;
}
PocketUiStatus pocket_layout_set(PocketLayoutContext *context, PocketUiHandle node,
                                 const PocketLayoutSpec *spec) {
    LayoutImpl *impl=li(context); PocketUiSnapshot snap;
    if(!impl || !spec || !spec_valid(spec)) return POCKET_UI_INVALID_ARGUMENT;
    if(pocket_ui_snapshot(impl->tree,node,&snap)!=POCKET_UI_OK) return POCKET_UI_STALE_HANDLE;
    LayoutRecord *r=find_record(impl,node,1);
    if(!r)return POCKET_UI_RESOURCE_EXHAUSTED;
    r->spec=*spec; r->has_result=0;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_layout_get(const PocketLayoutContext *context, PocketUiHandle node,
                                 PocketLayoutSpec *out) {
    const LayoutImpl *impl=cli(context); PocketUiSnapshot snap;
    if(!impl || !out)return POCKET_UI_INVALID_ARGUMENT;
    if(pocket_ui_snapshot(impl->tree,node,&snap)!=POCKET_UI_OK)return POCKET_UI_STALE_HANDLE;
    const LayoutRecord *r=find_record_const(impl,node);
    *out=r?r->spec:pocket_layout_spec_default();
    return POCKET_UI_OK;
}
PocketUiStatus pocket_layout_result(const PocketLayoutContext *context, PocketUiHandle node,
                                    PocketLayoutResult *out) {
    const LayoutImpl *impl=cli(context);
    PocketUiSnapshot snapshot;
    if(!impl || !out)return POCKET_UI_INVALID_ARGUMENT;
    PocketUiStatus live=pocket_ui_snapshot(impl->tree,node,&snapshot);
    if(live!=POCKET_UI_OK)return live==POCKET_UI_STALE_HANDLE?POCKET_UI_STALE_HANDLE:POCKET_UI_LIFECYCLE_ERROR;
    const LayoutRecord *r=find_record_const(impl,node);
    if(!r || !r->has_result)return POCKET_UI_LIFECYCLE_ERROR;
    *out=r->result; return POCKET_UI_OK;
}
static int32_t clamp_i32(int64_t v) {
    if(v<INT32_MIN)return INT32_MIN;
    if(v>INT32_MAX)return INT32_MAX;
    return (int32_t)v;
}
static int32_t clamp_size(int32_t value, int32_t minv, int32_t maxv) {
    if(value<minv)value=minv;
    if(maxv>=0 && value>maxv)value=maxv;
    return value<0?0:value;
}
static int32_t resolve_length(PocketLength length, int32_t parent, int32_t content) {
    switch(length.kind){
        case POCKET_LENGTH_PX:return length.value;
        case POCKET_LENGTH_PERCENT:return clamp_i32((int64_t)parent*length.value/10000);
        case POCKET_LENGTH_CONTENT:
        case POCKET_LENGTH_AUTO:return content;
        case POCKET_LENGTH_FILL:return 0;
        default:return 0;
    }
}
static PocketUiRect inset_rect(PocketUiRect r, PocketLayoutInsets i) {
    int64_t x=(int64_t)r.x+i.left, y=(int64_t)r.y+i.top;
    int64_t w=(int64_t)r.width-i.left-i.right, h=(int64_t)r.height-i.top-i.bottom;
    PocketUiRect out={clamp_i32(x),clamp_i32(y),w>0?clamp_i32(w):0,h>0?clamp_i32(h):0};
    return out;
}
static PocketUiRect intersect_rect(PocketUiRect a, PocketUiRect b) {
    int64_t x1=a.x>b.x?a.x:b.x, y1=a.y>b.y?a.y:b.y;
    int64_t ax2=(int64_t)a.x+a.width, bx2=(int64_t)b.x+b.width;
    int64_t ay2=(int64_t)a.y+a.height, by2=(int64_t)b.y+b.height;
    int64_t x2=ax2<bx2?ax2:bx2, y2=ay2<by2?ay2:by2;
    PocketUiRect r={clamp_i32(x1),clamp_i32(y1),x2>x1?clamp_i32(x2-x1):0,y2>y1?clamp_i32(y2-y1):0};
    return r;
}
static int collect_children(LayoutImpl *impl, PocketUiHandle parent,
                            PocketUiHandle *out, uint32_t *count) {
    PocketUiHandle child={0}; uint32_t n=0;
    if(pocket_ui_first_child(impl->tree,parent,&child)!=POCKET_UI_OK)return 0;
    while(pocket_ui_handle_valid(child)){
        if(n>=POCKET_LAYOUT_MAX_CHILDREN)return 0;
        out[n++]=child;
        PocketUiHandle next={0};
        if(pocket_ui_next_sibling(impl->tree,child,&next)!=POCKET_UI_OK)return 0;
        child=next;
    }
    for(uint32_t i=0;i<n/2;i++){PocketUiHandle t=out[i];out[i]=out[n-1U-i];out[n-1U-i]=t;}
    *count=n; return 1;
}
static PocketLayoutSpec spec_for(LayoutImpl *impl, PocketUiHandle node) {
    LayoutRecord *r=find_record(impl,node,0);
    return r?r->spec:pocket_layout_spec_default();
}
static int measure_content(LayoutImpl *impl, PocketUiHandle node, int32_t maxw, int32_t maxh,
                           int32_t *w, int32_t *h) {
    *w=*h=0;
    if(!impl->measure)return 1;
    return impl->measure(impl->measure_context,node,maxw,maxh,w,h) && *w>=0 && *h>=0;
}
static int desired_size(LayoutImpl *impl, PocketUiHandle node, const PocketLayoutSpec *s,
                        int32_t parent_w, int32_t parent_h, int32_t *w, int32_t *h) {
    int32_t cw=0,ch=0;
    if(!measure_content(impl,node,parent_w,parent_h,&cw,&ch))return 0;
    int32_t rw=resolve_length(s->width,parent_w,cw);
    int32_t rh=resolve_length(s->height,parent_h,ch);
    if(s->aspect_num && s->aspect_den){
        if((s->height.kind==POCKET_LENGTH_AUTO || s->height.kind==POCKET_LENGTH_CONTENT ||
            s->height.kind==POCKET_LENGTH_FILL) && rw>0)
            rh=clamp_i32((int64_t)rw*s->aspect_den/s->aspect_num);
        else if((s->width.kind==POCKET_LENGTH_AUTO || s->width.kind==POCKET_LENGTH_CONTENT ||
                 s->width.kind==POCKET_LENGTH_FILL) && rh>0)
            rw=clamp_i32((int64_t)rh*s->aspect_num/s->aspect_den);
    }
    *w=clamp_size(rw,s->min_width,s->max_width);
    *h=clamp_size(rh,s->min_height,s->max_height);
    return 1;
}
static PocketLayoutAlign effective_align(PocketLayoutSpec child, PocketLayoutSpec parent) {
    return child.align_self==POCKET_ALIGN_AUTO?parent.align_items:child.align_self;
}
static int apply_geometry(LayoutImpl *impl, PocketUiHandle node, PocketUiRect geometry) {
    PocketUiSnapshot snap;
    if(pocket_ui_snapshot(impl->tree,node,&snap)!=POCKET_UI_OK)return 0;
    PocketUiProperties p=snap.properties; p.geometry=geometry;
    return pocket_ui_update(impl->tree,node,POCKET_UI_PROP_GEOMETRY,&p)==POCKET_UI_OK;
}
static PocketUiStatus layout_node(LayoutImpl *impl, PocketUiHandle node, PocketUiRect geometry,
                                  PocketUiRect inherited_clip);

static PocketUiStatus place_child(LayoutImpl *impl, PocketUiHandle child, PocketUiRect geometry,
                                  PocketUiRect inherited_clip) {
    return layout_node(impl,child,geometry,inherited_clip);
}
static void distribute_flex(FlexChild *items, uint32_t start, uint32_t end,
                            int32_t available, int32_t gap) {
    int64_t used=0; uint64_t grow=0,shrink=0;
    for(uint32_t i=start;i<end;i++){
        used += items[i].main + items[i].before_main + items[i].after_main;
        grow += items[i].flex_grow; shrink += items[i].flex_shrink;
    }
    if(end>start+1)used+=(int64_t)gap*(end-start-1U);
    int64_t free=(int64_t)available-used;
    if(free>0 && grow){
        int64_t distributed=0; uint64_t seen=0;
        for(uint32_t i=start;i<end;i++)if(items[i].flex_grow){
            seen+=items[i].flex_grow;
            int64_t target=(free*(int64_t)seen)/(int64_t)grow;
            int64_t add=target-distributed; distributed=target;
            items[i].main=clamp_size(clamp_i32((int64_t)items[i].main+add),
                                     items[i].min_main,items[i].max_main);
        }
    } else if(free<0 && shrink){
        int64_t need=-free,distributed=0; uint64_t seen=0;
        for(uint32_t i=start;i<end;i++)if(items[i].flex_shrink){
            seen+=items[i].flex_shrink;
            int64_t target=(need*(int64_t)seen)/(int64_t)shrink;
            int64_t sub=target-distributed; distributed=target;
            items[i].main=clamp_size(items[i].main>sub?items[i].main-(int32_t)sub:0,
                                     items[i].min_main,items[i].max_main);
        }
    }
}
static PocketUiStatus layout_flex(LayoutImpl *impl, PocketUiHandle *children, uint32_t count,
                                  PocketLayoutSpec parent, PocketUiRect inner,
                                  PocketUiRect child_clip, int row) {
    FlexChild items[POCKET_LAYOUT_MAX_CHILDREN];
    for(uint32_t i=0;i<count;i++){
        PocketLayoutSpec s=spec_for(impl,children[i]); int32_t w=0,h=0;
        if(!desired_size(impl,children[i],&s,inner.width,inner.height,&w,&h))
            return POCKET_UI_INVALID_ARGUMENT;
        items[i].node=children[i];items[i].spec=s;
        items[i].main=row?w:h;items[i].cross=row?h:w;
        items[i].before_main=row?s.margin.left:s.margin.top;
        items[i].after_main=row?s.margin.right:s.margin.bottom;
        items[i].before_cross=row?s.margin.top:s.margin.left;
        items[i].after_cross=row?s.margin.bottom:s.margin.right;
        items[i].flex_grow=s.grow+(row?s.width.kind==POCKET_LENGTH_FILL:s.height.kind==POCKET_LENGTH_FILL);
        items[i].flex_shrink=s.shrink;
        items[i].min_main=row?s.min_width:s.min_height;
        items[i].max_main=row?s.max_width:s.max_height;
        items[i].min_cross=row?s.min_height:s.min_width;
        items[i].max_cross=row?s.max_height:s.max_width;
    }
    int32_t available_main=row?inner.width:inner.height;
    int32_t available_cross=row?inner.height:inner.width;
    uint32_t line_start=0; int32_t cross_cursor=0;
    while(line_start<count){
        uint32_t line_end=line_start; int64_t base=0; int32_t line_cross=0;
        while(line_end<count){
            int64_t add=items[line_end].main+items[line_end].before_main+items[line_end].after_main;
            if(line_end>line_start)add+=parent.gap;
            if(parent.wrap && line_end>line_start && base+add>available_main)break;
            base+=add;
            int32_t total_cross=items[line_end].cross+items[line_end].before_cross+items[line_end].after_cross;
            if(total_cross>line_cross)line_cross=total_cross;
            line_end++;
        }
        if(line_end==line_start)line_end++;
        if(!parent.wrap)line_cross=available_cross;
        distribute_flex(items,line_start,line_end,available_main,parent.gap);
        int64_t used=0;
        for(uint32_t i=line_start;i<line_end;i++)
            used+=items[i].main+items[i].before_main+items[i].after_main;
        if(line_end>line_start+1)used+=(int64_t)parent.gap*(line_end-line_start-1U);
        int32_t remain=available_main>used?available_main-(int32_t)used:0;
        int32_t cursor=0,gap=parent.gap;
        if(parent.justify_content==POCKET_JUSTIFY_CENTER)cursor=remain/2;
        else if(parent.justify_content==POCKET_JUSTIFY_END)cursor=remain;
        else if(parent.justify_content==POCKET_JUSTIFY_SPACE_BETWEEN && line_end-line_start>1)
            gap+=remain/(int32_t)(line_end-line_start-1U);
        for(uint32_t i=line_start;i<line_end;i++){
            PocketLayoutAlign align=effective_align(items[i].spec,parent);
            int32_t cross=items[i].cross;
            int32_t cross_space=line_cross-items[i].before_cross-items[i].after_cross;
            if(align==POCKET_ALIGN_STRETCH &&
               (row?items[i].spec.height.kind:items[i].spec.width.kind) != POCKET_LENGTH_PX &&
               cross_space>0)
                cross=clamp_size(cross_space,items[i].min_cross,items[i].max_cross);
            if(items[i].spec.aspect_num && items[i].spec.aspect_den &&
               (row?items[i].spec.height.kind:items[i].spec.width.kind) != POCKET_LENGTH_PX) {
                int32_t aspect_cross=row?
                    clamp_i32((int64_t)items[i].main*items[i].spec.aspect_den/items[i].spec.aspect_num):
                    clamp_i32((int64_t)items[i].main*items[i].spec.aspect_num/items[i].spec.aspect_den);
                cross=clamp_size(aspect_cross,items[i].min_cross,items[i].max_cross);
            }
            int32_t cross_offset=items[i].before_cross;
            if(align==POCKET_ALIGN_CENTER && cross_space>cross)cross_offset+=(cross_space-cross)/2;
            else if(align==POCKET_ALIGN_END && cross_space>cross)cross_offset+=cross_space-cross;
            cursor+=items[i].before_main;
            PocketUiRect rect;
            if(row)rect=(PocketUiRect){inner.x+cursor,inner.y+cross_cursor+cross_offset,items[i].main,cross};
            else rect=(PocketUiRect){inner.x+cross_cursor+cross_offset,inner.y+cursor,cross,items[i].main};
            PocketUiStatus st=place_child(impl,items[i].node,rect,child_clip);
            if(st!=POCKET_UI_OK)return st;
            cursor+=items[i].main+items[i].after_main+gap;
        }
        cross_cursor+=line_cross+(parent.wrap?parent.gap:0);
        line_start=line_end;
        if(!parent.wrap)break;
    }
    return POCKET_UI_OK;
}
static PocketUiStatus layout_stack(LayoutImpl *impl, PocketUiHandle *children, uint32_t count,
                                   PocketLayoutSpec parent, PocketUiRect inner,
                                   PocketUiRect child_clip) {
    for(uint32_t i=0;i<count;i++){
        PocketLayoutSpec s=spec_for(impl,children[i]);int32_t w=0,h=0;
        if(!desired_size(impl,children[i],&s,inner.width,inner.height,&w,&h))
            return POCKET_UI_INVALID_ARGUMENT;
        PocketLayoutAlign align=effective_align(s,parent);
        int32_t aw=inner.width-s.margin.left-s.margin.right;
        int32_t ah=inner.height-s.margin.top-s.margin.bottom;
        if(align==POCKET_ALIGN_STRETCH){
            if(s.width.kind!=POCKET_LENGTH_PX)w=clamp_size(aw>0?aw:0,s.min_width,s.max_width);
            if(s.height.kind!=POCKET_LENGTH_PX)h=clamp_size(ah>0?ah:0,s.min_height,s.max_height);
        }
        int32_t x=inner.x+s.margin.left,y=inner.y+s.margin.top;
        if(align==POCKET_ALIGN_CENTER){if(aw>w)x+=(aw-w)/2;if(ah>h)y+=(ah-h)/2;}
        else if(align==POCKET_ALIGN_END){if(aw>w)x+=aw-w;if(ah>h)y+=ah-h;}
        PocketUiStatus st=place_child(impl,children[i],(PocketUiRect){x,y,w,h},child_clip);
        if(st!=POCKET_UI_OK)return st;
    }
    return POCKET_UI_OK;
}
static PocketUiStatus layout_absolute(LayoutImpl *impl, PocketUiHandle *children, uint32_t count,
                                      PocketUiRect inner, PocketUiRect child_clip) {
    for(uint32_t i=0;i<count;i++){
        PocketLayoutSpec s=spec_for(impl,children[i]);int32_t w=0,h=0;
        if(!desired_size(impl,children[i],&s,inner.width,inner.height,&w,&h))
            return POCKET_UI_INVALID_ARGUMENT;
        int32_t ox=resolve_length(s.offset_x,inner.width,0);
        int32_t oy=resolve_length(s.offset_y,inner.height,0);
        PocketUiRect r={inner.x+ox+s.margin.left,inner.y+oy+s.margin.top,w,h};
        PocketUiStatus st=place_child(impl,children[i],r,child_clip);
        if(st!=POCKET_UI_OK)return st;
    }
    return POCKET_UI_OK;
}
static PocketUiStatus layout_grid(LayoutImpl *impl, PocketUiHandle *children, uint32_t count,
                                  PocketLayoutSpec parent, PocketUiRect inner,
                                  PocketUiRect child_clip) {
    uint32_t cols=parent.grid_columns?parent.grid_columns:1U;
    int32_t cellw=(inner.width-(int32_t)(cols-1U)*parent.gap)/(int32_t)cols;
    if(cellw<0)cellw=0;
    uint32_t index=0; int32_t y=inner.y;
    while(index<count){
        uint32_t end=index+cols;if(end>count)end=count;int32_t rowh=0;
        for(uint32_t i=index;i<end;i++){
            PocketLayoutSpec s=spec_for(impl,children[i]);int32_t w=0,h=0;
            if(!desired_size(impl,children[i],&s,cellw,inner.height,&w,&h))return POCKET_UI_INVALID_ARGUMENT;
            int32_t total=h+s.margin.top+s.margin.bottom;if(total>rowh)rowh=total;
        }
        for(uint32_t i=index;i<end;i++){
            PocketLayoutSpec s=spec_for(impl,children[i]);int32_t w=0,h=0;
            if(!desired_size(impl,children[i],&s,cellw,rowh,&w,&h))return POCKET_UI_INVALID_ARGUMENT;
            uint32_t col=i-index;int32_t cellx=inner.x+(int32_t)col*(cellw+parent.gap);
            int32_t aw=cellw-s.margin.left-s.margin.right,ah=rowh-s.margin.top-s.margin.bottom;
            PocketLayoutAlign align=effective_align(s,parent);
            if(align==POCKET_ALIGN_STRETCH && s.width.kind!=POCKET_LENGTH_PX)
                w=clamp_size(aw>0?aw:0,s.min_width,s.max_width);
            int32_t x=cellx+s.margin.left,y0=y+s.margin.top;
            if(align==POCKET_ALIGN_CENTER){if(aw>w)x+=(aw-w)/2;if(ah>h)y0+=(ah-h)/2;}
            else if(align==POCKET_ALIGN_END){if(aw>w)x+=aw-w;if(ah>h)y0+=ah-h;}
            PocketUiStatus st=place_child(impl,children[i],(PocketUiRect){x,y0,w,h},child_clip);
            if(st!=POCKET_UI_OK)return st;
        }
        y+=rowh+parent.gap;index=end;
    }
    return POCKET_UI_OK;
}
static PocketUiStatus layout_node(LayoutImpl *impl, PocketUiHandle node, PocketUiRect geometry,
                                  PocketUiRect inherited_clip) {
    LayoutRecord *record=find_record(impl,node,1);
    if(!record)return POCKET_UI_RESOURCE_EXHAUSTED;
    PocketLayoutSpec spec=record->spec;
    if(!apply_geometry(impl,node,geometry))return POCKET_UI_LIFECYCLE_ERROR;
    record=find_record(impl,node,0);
    if(!record)return POCKET_UI_RESOURCE_EXHAUSTED;
    record->result.geometry=geometry;
    record->result.clip=intersect_rect(geometry,inherited_clip);
    record->result.clip_valid=1;
    record->has_result=1;

    PocketUiHandle children[POCKET_LAYOUT_MAX_CHILDREN];uint32_t count=0;
    if(!collect_children(impl,node,children,&count))return POCKET_UI_RESOURCE_EXHAUSTED;
    PocketUiRect inner=inset_rect(geometry,spec.padding);
    PocketUiRect child_clip=inherited_clip;
    if(spec.overflow==POCKET_OVERFLOW_CLIP)child_clip=intersect_rect(inherited_clip,geometry);
    PocketUiStatus st=POCKET_UI_OK;
    if(count){
        switch(spec.mode){
            case POCKET_LAYOUT_ROW:st=layout_flex(impl,children,count,spec,inner,child_clip,1);break;
            case POCKET_LAYOUT_COLUMN:st=layout_flex(impl,children,count,spec,inner,child_clip,0);break;
            case POCKET_LAYOUT_STACK:st=layout_stack(impl,children,count,spec,inner,child_clip);break;
            case POCKET_LAYOUT_GRID:st=layout_grid(impl,children,count,spec,inner,child_clip);break;
            case POCKET_LAYOUT_ABSOLUTE:st=layout_absolute(impl,children,count,inner,child_clip);break;
            case POCKET_LAYOUT_LEAF:return POCKET_UI_INVALID_ARGUMENT;
            default:return POCKET_UI_INVALID_ARGUMENT;
        }
        if(st!=POCKET_UI_OK)return st;
    }
    return pocket_ui_layout(impl->tree,node);
}
PocketUiStatus pocket_layout_run(PocketLayoutContext *context, PocketUiHandle root,
                                 int32_t viewport_width, int32_t viewport_height) {
    LayoutImpl *impl=li(context);PocketUiSnapshot snap;
    if(!impl || viewport_width<=0 || viewport_height<=0)return POCKET_UI_INVALID_ARGUMENT;
    if((int64_t)impl->safe.left+impl->safe.right>=viewport_width ||
       (int64_t)impl->safe.top+impl->safe.bottom>=viewport_height)
        return POCKET_UI_INVALID_ARGUMENT;
    if(pocket_ui_snapshot(impl->tree,root,&snap)!=POCKET_UI_OK)return POCKET_UI_STALE_HANDLE;
    PocketUiRect viewport={0,0,viewport_width,viewport_height};
    PocketUiRect safe=inset_rect(viewport,impl->safe);
    return layout_node(impl,root,safe,viewport);
}
