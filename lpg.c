#include "lpg.h"

/**
 * @brief 初始化lpg结构体
 * @param lpg 指向lpg结构体的指针
 * @param loop_time 心跳周期 (ms)
 */
void lpg_init(lpg_t* lpg, uint16_t loop_time)
{
    lpg->unit_num = 0;
    lpg->loop_time = loop_time;
}

/**
 * @brief 注册一个lpg单元
 * @param lpg 指向lpg结构体的指针
 * @param segments 段描述数组指针 {{duration, level}, ...}
 * @param seg_num 段数量
 * @param up 上升沿回调
 * @param down 下降沿回调
 */
void lpg_register(lpg_t* lpg, const lpg_seg_t* segments, uint16_t seg_num, lpg_cb_t up, lpg_cb_t down)
{    
    lpg_unit_t* unit = &lpg->units[lpg->unit_num];
    unit->segments = segments;
    unit->seg_num = seg_num;
    unit->up = up;
    unit->down = down;
    unit->seg_index = 0;
    unit->tick_count = 0;
    unit->level = segments[0].level; // 初始电平
    unit->level_pre = segments[0].level;
    lpg->unit_num++;
}

/**
 * @brief 设置lpg单元的段序列
 * @param unit 指向lpg单元的指针
 * @param segments 新的段描述数组指针
 * @param seg_num 段数量
 */
void lpg_set_pattern(lpg_unit_t* unit, const lpg_seg_t* segments, uint16_t seg_num)
{
    unit->segments = segments;
    unit->seg_num = seg_num;
    unit->seg_index = 0;
    unit->tick_count = 0;
    unit->level = segments[0].level;
    unit->level_pre = segments[0].level;
}

/**
 * @brief lpg主循环（需周期性调用）
 * @param lpg 指向lpg结构体的指针
 */
void lpg_loop(lpg_t* lpg)
{
    for(uint16_t i = 0; i < lpg->unit_num; i++)
    {
        lpg_unit_t* unit = &lpg->units[i];
        unit->level_pre = unit->level;
        
        if (unit->tick_count >= unit->segments[unit->seg_index].duration)
        {
            unit->tick_count = 0;
            unit->seg_index = (unit->seg_index + 1) % unit->seg_num;
            unit->level = unit->segments[unit->seg_index].level;
        }

        if(lpg->first_run == 0)
        {
            lpg->first_run = 1;
            if(unit->level == 1 && unit->up) 
            {
                unit->up();
            }
            else if(unit->level == 0 && unit->down) 
            {
                unit->down();
            }
        }
        
        if(unit->level_pre == 0 && unit->level == 1 && unit->up) 
        {
            unit->up();
        }
        else if(unit->level_pre == 1 && unit->level == 0 && unit->down) 
        {
            unit->down();
        }

        unit->tick_count++;
    }
}
