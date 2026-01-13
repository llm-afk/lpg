#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

// ================= 平台兼容性 (Windows/Linux) =================
#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
    #define SLEEP_MS(ms) Sleep(ms)
    
    // Windows 键盘检测
    int kbhit_key(void) {
        if (_kbhit()) return _getch();
        return 0;
    }
    
    // Windows 控制台初始化
    void platform_init(void) {
        system("cls");
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#else
    #include <unistd.h>
    #include <termios.h>
    #define SLEEP_MS(ms) usleep((ms) * 1000)

    // Linux 键盘检测配置
    static struct termios orig_termios;
    void disable_raw_mode(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }
    void enable_raw_mode(void) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        atexit(disable_raw_mode);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    int kbhit_key(void) {
        char c;
        return (read(STDIN_FILENO, &c, 1) == 1) ? c : 0;
    }

    void platform_init(void) {
        enable_raw_mode();
        printf("\033[2J\033[?25l"); // 清屏+隐光标
    }
#endif

// ================= LPG 核心结构定义 =================

#define LPG_MAX_UNITS    4
#define DISPLAY_WIDTH    60

// 前置声明
typedef struct lpg_unit lpg_unit_t;
typedef void (*lpg_cb_t)(lpg_unit_t* unit);

// 段描述结构体：{持续时间, 电平}
typedef struct {
    uint16_t duration;  // 持续多少个 tick
    uint16_t level;     // 输出什么电平 (0/1)
} lpg_seg_t;

struct lpg_unit {
    // --- 核心控制部分 ---
    const lpg_seg_t* segments;  // 指向段数组
    uint16_t seg_num;           // 数组长度
    uint16_t seg_index;         // 当前走到第几段
    uint16_t tick_count;        // 当前段已走了多少tick
    
    uint16_t level;             // 当前输出电平
    uint16_t level_pre;         // 上一次电平
    
    lpg_cb_t up;                // 回调
    lpg_cb_t down;              // 回调

    // --- UI 显示部分 ---
    char name[16];                  // 单元名称
    uint8_t history[DISPLAY_WIDTH]; // 历史波形记录
};

typedef struct {
    lpg_unit_t units[LPG_MAX_UNITS];
    uint16_t unit_num;
    uint16_t loop_time;
    uint32_t total_ticks;
} lpg_t;

// ================= LPG 核心逻辑实现 =================

void lpg_init(lpg_t* lpg, uint16_t loop_time)
{
    memset(lpg, 0, sizeof(lpg_t));
    lpg->loop_time = loop_time;
}

int lpg_register(lpg_t* lpg, const char* name, const lpg_seg_t* segments, uint16_t seg_num, lpg_cb_t up, lpg_cb_t down)
{
    if (lpg->unit_num >= LPG_MAX_UNITS) return -1;
    
    lpg_unit_t* unit = &lpg->units[lpg->unit_num];
    
    // UI 初始化
    strncpy(unit->name, name, sizeof(unit->name) - 1);
    memset(unit->history, 0, DISPLAY_WIDTH);

    // 逻辑初始化
    unit->segments = segments;
    unit->seg_num = seg_num;
    unit->up = up;
    unit->down = down;
    
    // 初始状态复位
    unit->seg_index = 0;
    unit->tick_count = 0;
    unit->level = (seg_num > 0) ? segments[0].level : 0;
    unit->level_pre = unit->level;

    return lpg->unit_num++;
}

void lpg_set_pattern(lpg_t* lpg, uint16_t unit_idx, const lpg_seg_t* segments, uint16_t seg_num)
{
    if (unit_idx >= lpg->unit_num) return;
    
    lpg_unit_t* unit = &lpg->units[unit_idx];
    unit->segments = segments;
    unit->seg_num = seg_num;
    
    // 切换模式时必须重置计数器
    unit->seg_index = 0;
    unit->tick_count = 0;
    // 立即应用新模式的初始电平，防止延迟
    unit->level = (seg_num > 0) ? segments[0].level : 0;
}

void lpg_loop(lpg_t* lpg)
{
    for (uint16_t i = 0; i < lpg->unit_num; i++)
    {
        lpg_unit_t* unit = &lpg->units[i];

        // --- 1. UI 历史数据左移 (为了滚动显示) ---
        memmove(unit->history, unit->history + 1, DISPLAY_WIDTH - 1);

        // --- 2. 核心状态机逻辑 ---
        unit->level_pre = unit->level;

        // 检查当前段的时间是否结束
        if (unit->tick_count >= unit->segments[unit->seg_index].duration)
        {
            unit->tick_count = 0;
            // 切换到下一段
            unit->seg_index = (unit->seg_index + 1) % unit->seg_num;
            // 更新电平
            unit->level = unit->segments[unit->seg_index].level;
        }

        // --- 3. 边沿检测回调 ---
        if (unit->level_pre == 0 && unit->level == 1 && unit->up) unit->up(unit);
        else if (unit->level_pre == 1 && unit->level == 0 && unit->down) unit->down(unit);

        unit->tick_count++;

        // --- 4. 记录最新电平到 UI 历史 ---
        unit->history[DISPLAY_WIDTH - 1] = (uint8_t)unit->level;
    }
    lpg->total_ticks++;
}

// ================= UI 显示模块 =================

void display_waveform(lpg_t* lpg, const char* mode_names[])
{
    printf("\033[H");  // 光标复位到左上角

    printf("\033[1;36m+-----------------------------------------------------------------------+\033[0m\n");
    printf("\033[1;36m|\033[0m \033[1;33mLPG Sim\033[0m (Segment Based) Tick: \033[1;32m%08u\033[0m                          \033[1;36m|\033[0m\n", lpg->total_ticks);
    printf("\033[1;36m+-----------------------------------------------------------------------+\033[0m\n");

    for (uint16_t i = 0; i < lpg->unit_num; i++)
    {
        lpg_unit_t* unit = &lpg->units[i];
        
        // 打印信息头
        printf("\033[1;36m|\033[0m \033[1;37m%-4s\033[0m [\033[1;%dm%s\033[0m] \033[35m%-8s\033[0m ", 
               unit->name, 
               unit->level ? 31 : 32, // 31红 32绿
               unit->level ? "H" : "L", 
               mode_names[i]);

        // 打印滚动波形
        for (int j = 0; j < DISPLAY_WIDTH; j++)
        {
            if (unit->history[j])
                printf("\033[41m \033[0m");  // 红色背景块 (高电平)
            else
                printf("\033[90m_\033[0m");  // 灰色下划线 (低电平)
        }
        printf(" \033[1;36m|\033[0m\n");
    }

    printf("\033[1;36m+-----------------------------------------------------------------------+\033[0m\n");
    printf("\033[1;36m|\033[0m Controls: [1][2][3] Change Pattern   [Q] Quit                          \033[1;36m|\033[0m\n");
    printf("\033[1;36m+-----------------------------------------------------------------------+\033[0m\n");
    
    fflush(stdout);
}

// ================= 主程序数据与逻辑 =================

// --- 定义新的段模式 {duration, level} ---

// IO 1: 基础闪烁
const lpg_seg_t p1_slow[] = { {10, 1}, {10, 0} };             // 慢闪 (1s 亮 1s 灭, 假设100ms周期)
const lpg_seg_t p1_fast[] = { {2, 1}, {2, 0} };               // 快闪
const lpg_seg_t p1_pwm[]  = { {1, 1}, {3, 0} };               // 25% 占空比

// IO 2: 复杂序列
const lpg_seg_t p2_2blink[] = { {2, 1}, {2, 0}, {2, 1}, {10, 0} }; // 双闪
const lpg_seg_t p2_3blink[] = { {2, 1}, {2, 0}, {2, 1}, {2, 0}, {2, 1}, {10, 0} }; // 三闪
const lpg_seg_t p2_breath[] = { {5, 1}, {1, 0}, {4, 1}, {2, 0}, {3, 1}, {3, 0}, {2, 1}, {4, 0}, {1, 1}, {5, 0} }; // 伪呼吸

// IO 3: 常规状态
const lpg_seg_t p3_on[]   = { {100, 1} }; // 常亮
const lpg_seg_t p3_off[]  = { {100, 0} }; // 常灭
const lpg_seg_t p3_sos[]  = { 
    {2,1},{2,0},{2,1},{2,0},{2,1},{2,0},       // S
    {6,1},{2,0},{6,1},{2,0},{6,1},{2,0},       // O
    {2,1},{2,0},{2,1},{2,0},{2,1},{2,0},       // S
    {20, 0}                                    // Wait
};

// 模式管理结构
typedef struct { 
    const lpg_seg_t* segs; 
    uint16_t len; 
    const char* name; 
} mode_cfg_t;

#define SEG_LEN(arr) (sizeof(arr)/sizeof(lpg_seg_t))

static const mode_cfg_t modes1[] = {
    {p1_slow, SEG_LEN(p1_slow), "Slow"},
    {p1_fast, SEG_LEN(p1_fast), "Fast"},
    {p1_pwm,  SEG_LEN(p1_pwm),  "PWM25%"}
};

static const mode_cfg_t modes2[] = {
    {p2_2blink, SEG_LEN(p2_2blink), "2-Blink"},
    {p2_3blink, SEG_LEN(p2_3blink), "3-Blink"},
    {p2_breath, SEG_LEN(p2_breath), "Breath"}
};

static const mode_cfg_t modes3[] = {
    {p3_on,  SEG_LEN(p3_on),  "ON"},
    {p3_off, SEG_LEN(p3_off), "OFF"},
    {p3_sos, SEG_LEN(p3_sos), "SOS"}
};

// 信号处理
static volatile int running = 1;
void signal_handler(int sig) { (void)sig; running = 0; }

int main()
{
    signal(SIGINT, signal_handler);
    platform_init();

    lpg_t lpg;
    lpg_init(&lpg, 100); // 100ms per tick

    // 初始注册
    lpg_register(&lpg, "LED1", modes1[0].segs, modes1[0].len, NULL, NULL);
    lpg_register(&lpg, "LED2", modes2[0].segs, modes2[0].len, NULL, NULL);
    lpg_register(&lpg, "BUZZ", modes3[0].segs, modes3[0].len, NULL, NULL);

    uint8_t idx1 = 0, idx2 = 0, idx3 = 0;
    const char* current_mode_names[3] = {modes1[0].name, modes2[0].name, modes3[0].name};

    while (running)
    {
        // 1. 处理输入
        int key = kbhit_key();
        if (key) {
            if (key == '1') {
                idx1 = (idx1 + 1) % 3;
                lpg_set_pattern(&lpg, 0, modes1[idx1].segs, modes1[idx1].len);
                current_mode_names[0] = modes1[idx1].name;
            }
            else if (key == '2') {
                idx2 = (idx2 + 1) % 3;
                lpg_set_pattern(&lpg, 1, modes2[idx2].segs, modes2[idx2].len);
                current_mode_names[1] = modes2[idx2].name;
            }
            else if (key == '3') {
                idx3 = (idx3 + 1) % 3;
                lpg_set_pattern(&lpg, 2, modes3[idx3].segs, modes3[idx3].len);
                current_mode_names[2] = modes3[idx3].name;
            }
            else if (key == 'q' || key == 'Q') {
                running = 0;
            }
        }

        // 2. 核心逻辑循环
        lpg_loop(&lpg);

        // 3. 刷新显示
        display_waveform(&lpg, current_mode_names);
        
        // 4. 模拟时间流逝
        SLEEP_MS(lpg.loop_time);
    }

    // 退出清理
    printf("\033[?25h\033[0m\n"); // 恢复光标
    printf("Simulation Finished.\n");
    
    return 0;
}
