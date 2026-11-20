#include <assert.h>
#include <stdio.h>
#include "dashboard_map_view.h"
#include "assets/ukraine_map_data.h"
#include "assets/dashboard_threat_icons.h"

int main(void)
{
    int min_x=424,min_y=222,max_x=-1,max_y=-1,x=0,y=0;
    for (unsigned i=0;i<sizeof(ua_map_runs);i+=2) {
        for (unsigned j=0;j<ua_map_runs[i];++j) {
            if (ua_map_runs[i+1]) {
                if(x<min_x)min_x=x; if(x>max_x)max_x=x;
                if(y<min_y)min_y=y; if(y>max_y)max_y=y;
            }
            if(++x==424) {x=0;++y;}
        }
    }
    assert(y==222 && x==0);
    assert(min_x==DASH_MAP_LEFT && min_y==DASH_MAP_TOP);
    assert(max_x-min_x+1==DASH_MAP_CONTENT_WIDTH && max_y-min_y+1==DASH_MAP_CONTENT_HEIGHT);
    for(int my=min_y;my<=max_y;++my) for(int mx=min_x;mx<=max_x;++mx) {
        int px,py,rx,ry;dashboard_map_project(mx,my,&px,&py);
        assert(dashboard_map_source(px,py,&rx,&ry) && rx==mx && ry==my);
    }
    for(int sy=0;sy<DASH_MAP_VIEW_HEIGHT;++sy) for(int sx=0;sx<DASH_MAP_VIEW_WIDTH;++sx) {
        const bool inside=dashboard_map_source(sx,sy,&x,&y);
        assert(inside==(sy>=76 && sy<403));
        if(inside) {
            assert(x>=min_x && x<=max_x && y>=min_y && y<=max_y);
            int px,py; dashboard_map_project(x,y,&px,&py);
            assert(sx-px>=-1 && sx-px<=1 && sy-py>=-1 && sy-py<=1);
        }
    }
    dashboard_map_project(UA_MAP_DNIPRO_X,UA_MAP_DNIPRO_Y,&x,&y);
    assert(x==342 && y==232);
    assert(!dashboard_map_source(-1,200,&x,&y));
    assert(!dashboard_map_source(480,200,&x,&y));
    assert(!dashboard_map_source(20,480,&x,&y));
    dashboard_map_icon_t icons[DASH_MAP_ICON_COUNT];
    assert(dashboard_map_categories(0,icons)==0);
    assert(dashboard_map_categories((1u<<4)|(1u<<5)|(1u<<6),icons)==1 && icons[0]==DASH_MAP_ICON_ROCKET);
    unsigned n=dashboard_map_categories(0x3ff,icons);
    assert(n==6);
    unsigned seen=0;
    for(unsigned i=0;i<n;++i) {assert(!(seen&(1u<<icons[i])));seen|=1u<<icons[i];}
    assert(seen==0x3f);
    assert(dashboard_map_categories((1u<<7),icons)==1 && icons[0]==DASH_MAP_ICON_UAV);
    assert(dashboard_map_categories((1u<<1)|(1u<<2)|(1u<<3),icons)==1 && icons[0]==DASH_MAP_ICON_AIRCRAFT);
    dashboard_map_group_size(6,&x,&y);assert(x==78 && y==54);
    dashboard_map_group_size(1,&x,&y);assert(x==30 && y==30);
    dashboard_map_group_size(0,&x,&y);assert(x==0 && y==0);
    for(unsigned i=0;i<DASH_MAP_ICON_COUNT;++i) {
        const dashboard_icon_asset_t *asset=&dashboard_threat_icons[i];
        assert(asset->triangle_count+asset->segment_count>0);
        for(unsigned j=0;j<asset->triangle_count;++j) {
            const dashboard_icon_triangle_t *t=&asset->triangles[j];
            assert(t->x1>=0 && t->x1<24 && t->y1>=0 && t->y1<24);
            assert(t->x2>=0 && t->x2<24 && t->y2>=0 && t->y2<24);
            assert(t->x3>=0 && t->x3<24 && t->y3>=0 && t->y3<24);
        }
        for(unsigned j=0;j<asset->segment_count;++j) {
            const dashboard_icon_segment_t *line=&asset->segments[j];
            assert(line->x1>=0 && line->x1<24 && line->y1>=0 && line->y1<24);
            assert(line->x2>=0 && line->x2<24 && line->y2>=0 && line->y2<24);
        }
    }
    puts("Fixed map fit, source bounds and threat category tests passed");
    return 0;
}
