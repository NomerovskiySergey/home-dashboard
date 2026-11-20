/* Original 24x24 vector pictograms for this project. No external artwork/fonts. */
#pragma once
#include <stdint.h>
#include "dashboard_map_view.h"

typedef struct { int8_t x1,y1,x2,y2,x3,y3; } dashboard_icon_triangle_t;
typedef struct { int8_t x1,y1,x2,y2; } dashboard_icon_segment_t;
typedef struct {
    const dashboard_icon_triangle_t *triangles;
    unsigned triangle_count;
    const dashboard_icon_segment_t *segments;
    unsigned segment_count;
} dashboard_icon_asset_t;

static const dashboard_icon_triangle_t icon_rocket_fill[] = {
    {12,1,8,8,16,8}, {8,8,16,8,16,18}, {8,8,16,18,8,18},
    {8,13,8,21,3,21}, {16,13,21,21,16,21}, {10,19,14,19,12,23},
};
static const dashboard_icon_triangle_t icon_uav_fill[] = {
    {12,2,1,21,12,16}, {12,2,12,16,23,21}, {11,13,13,13,12,23},
};
static const dashboard_icon_triangle_t icon_aircraft_fill[] = {
    {12,1,10,7,14,7}, {10,7,14,7,12,23},
    {10,8,10,15,1,15}, {14,8,23,15,14,15},
    {11,18,12,22,6,22}, {13,18,18,22,12,22},
};
static const dashboard_icon_triangle_t icon_bomb_fill[] = {
    {9,7,17,7,17,16}, {9,7,17,16,9,16}, {9,16,17,16,13,21},
    {9,3,13,7,17,3},
};
static const dashboard_icon_segment_t icon_bomb_lines[] = {{13,3,13,9}};
static const dashboard_icon_segment_t icon_shield_lines[] = {
    {4,4,12,2},{12,2,20,4},{20,4,19,14},{19,14,12,22},
    {12,22,5,14},{5,14,4,4},{8,12,11,15},{11,15,16,9},
};
static const dashboard_icon_segment_t icon_unknown_lines[] = {
    {12,2,2,21},{2,21,22,21},{22,21,12,2},{12,8,12,14},{12,18,12,18},
};
#define DASH_ICON_N(a) (sizeof(a)/sizeof((a)[0]))
static const dashboard_icon_asset_t dashboard_threat_icons[DASH_MAP_ICON_COUNT] = {
    [DASH_MAP_ICON_ROCKET] = {icon_rocket_fill,DASH_ICON_N(icon_rocket_fill),0,0},
    [DASH_MAP_ICON_UAV] = {icon_uav_fill,DASH_ICON_N(icon_uav_fill),0,0},
    [DASH_MAP_ICON_AIRCRAFT] = {icon_aircraft_fill,DASH_ICON_N(icon_aircraft_fill),0,0},
    [DASH_MAP_ICON_BOMB] = {icon_bomb_fill,DASH_ICON_N(icon_bomb_fill),icon_bomb_lines,DASH_ICON_N(icon_bomb_lines)},
    [DASH_MAP_ICON_SHIELD] = {0,0,icon_shield_lines,DASH_ICON_N(icon_shield_lines)},
    [DASH_MAP_ICON_UNKNOWN] = {0,0,icon_unknown_lines,DASH_ICON_N(icon_unknown_lines)},
};
#undef DASH_ICON_N
