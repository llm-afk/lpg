#ifndef LPG_H
#define LPG_H

#include <stdint.h>

#define LPG_UNIT_MAX 4

typedef void (*lpg_cb_t)(void);

typedef struct{
    uint16_t duration;  // 持续时间 心跳数
    uint16_t level;     // 电平值 0/1
}lpg_seg_t;

typedef struct{
    const lpg_seg_t* segments;  // 段数组指针
    uint16_t seg_num;           // 段数量
    lpg_cb_t up;                // 上升沿回调
    lpg_cb_t down;              // 下降沿回调
    uint16_t seg_index;         // 当前段索引
    uint16_t tick_count;        // 当前段已计数的tick
    uint8_t  level;             // 当前电平
    uint8_t  level_pre;         // 上一次电平
}lpg_unit_t;

typedef struct{
    lpg_unit_t units[LPG_UNIT_MAX];
    uint16_t first_run;         // 首次运行标志
    uint16_t unit_num;          // 已注册单元数量
    uint16_t loop_time;         // 心跳周期 (ms)
}lpg_t;

void lpg_init(lpg_t* lpg, uint16_t loop_time);
void lpg_register(lpg_t* lpg, const lpg_seg_t* segments, uint16_t seg_num, lpg_cb_t up, lpg_cb_t down);
void lpg_set_pattern(lpg_unit_t* unit, const lpg_seg_t* segments, uint16_t seg_num);
void lpg_loop(lpg_t* lpg);

#endif