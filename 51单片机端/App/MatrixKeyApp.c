#include "MatrixKeyApp.h"
#include "Font_16x16.h"

// ============================================================
// 状态定义
// ============================================================
#define STATE_LOCKED     0   // 待机
#define STATE_INPUT      1   // 密码输入
#define STATE_UNLOCKED   2   // 已开锁
#define STATE_SET_PWD    3   // 输入新密码
#define STATE_SET_CONF   4   // 确认新密码
#define STATE_ERROR      5   // 密码错误
#define STATE_PWD_OK     6   // 密码修改成功
#define STATE_PWD_MISMATCH 7   // 两次密码不一致
#define STATE_LOCK_DOWN   8   // 错误过多已锁死
#define STATE_PAIRING     9   // 蓝牙配对模式（按 # 进入）
#define STATE_PAIRING_OK 10   // 配对成功

// ============================================================
// 按键功能映射 (矩阵键盘键码 1~16)
// ============================================================
#define KEY_CONFIRM   4
#define KEY_CANCEL    8
#define KEY_BACK     12
#define KEY_FUNC     16
#define KEY_PAIR     15

// ============================================================
// 全局状态变量
// ============================================================
static unsigned char state;
static unsigned char pwd_saved[6];       // 已保存的密码
static unsigned char pwd_buf[6];         // 当前输入缓冲
static unsigned char pwd_idx;            // 输入计数
static unsigned char new_pwd[6];         // 新密码缓冲 (首次输入)
static unsigned char new_pwd_idx;        // 新密码计数
static unsigned char error_cnt;          // 连续错误次数
static unsigned long state_timer;        // 状态进入时间戳 (uwtick)
static unsigned char last_displayed_sec;  // 锁死倒计时上次显示值

// 按键检测 (由调度器周期性调用)
unsigned char Key_Val, Key_Down, Key_Old, Key_Up;

// ============================================================
// 键码→数字 映射表 (0xFF = 非数字)
// ============================================================
static unsigned char code key_to_digit[17] = {
    0xFF,  //  0: 无效
    1,     //  1
    2,     //  2
    3,     //  3
    0xFF,  //  4: 确认
    4,     //  5
    5,     //  6
    6,     //  7
    0xFF,  //  8: 取消
    7,     //  9
    8,     // 10
    9,     // 11
    0xFF,  // 12: 退格
    0xFF,  // 13: *
    0,     // 14: 0
    0xFF,  // 15: #
    0xFF,  // 16: 功能
};

// ============================================================
// AT24C02 密码存储
// ============================================================
#define PWD_ADDR_MAGIC  0
#define PWD_ADDR_DATA   1
#define PWD_MAGIC_VAL   0xAA
#define PWD_LEN         6

static void load_password(void)
{
    unsigned char i;
    if (AT24C02_ReadByte(PWD_ADDR_MAGIC) != PWD_MAGIC_VAL)
    {
        // 首次上电，初始化密码 000000
        AT24C02_WriteByte(PWD_ADDR_MAGIC, PWD_MAGIC_VAL);
        Delay(5);
        for (i = 0; i < PWD_LEN; i++)
        {
            AT24C02_WriteByte(PWD_ADDR_DATA + i, 0);
            Delay(5);  // 每次写入后等待 EEPROM 完成
            pwd_saved[i] = 0;
        }
    }
    else
    {
        for (i = 0; i < PWD_LEN; i++)
            pwd_saved[i] = AT24C02_ReadByte(PWD_ADDR_DATA + i);
    }
}

static void save_password(unsigned char *buf)
{
    unsigned char i;
    for (i = 0; i < PWD_LEN; i++)
    {
        AT24C02_WriteByte(PWD_ADDR_DATA + i, buf[i]);
        Delay(5);
    }
}

static unsigned char check_password(void)
{
    unsigned char i;
    for (i = 0; i < PWD_LEN; i++)
    {
        if (pwd_buf[i] != pwd_saved[i])
            return 0;
    }
    return 1;
}

// ============================================================
// OLED 屏幕渲染 (16x16 汉字助手)
// ============================================================
static void show_chinese(unsigned char page, unsigned char x,
                         const unsigned char *idx_list, unsigned char count)
{
    unsigned char i;
    for (i = 0; i < count; i++)
        OLED_ShowImage_H16(page, x + i * 16, 16, font_data[idx_list[i]]);
}

// 显示一行 6x8 密码光标 ****__
static void show_cursor(unsigned char count)
{
    unsigned char i;
    unsigned char str[7];
    for (i = 0; i < count && i < 6; i++) str[i] = '*';
    for (; i < 6; i++) str[i] = '_';
    str[6] = '\0';
    OLED_ShowString(46, 3, str);  // 居中: (128-6*6)/2 = 46
}

// ============================================================
// 各状态画面
// ============================================================
static const unsigned char code scr_welcome[] = {
    FONT_IDX_ZHI, FONT_IDX_NENG, FONT_IDX_MEN, FONT_IDX_SUO,
};
static const unsigned char code scr_prompt[] = {
    FONT_IDX_AN, FONT_IDX_REN, FONT_IDX_YI, FONT_IDX_JIAN,
    FONT_IDX_SHU, FONT_IDX_RU,
};
static const unsigned char code scr_enter_pwd[] = {
    FONT_IDX_QING, FONT_IDX_SHU, FONT_IDX_RU, FONT_IDX_MI, FONT_IDX_MA,
};
static const unsigned char code scr_btn_row[] = {
    FONT_IDX_QUE, FONT_IDX_REN2,
    FONT_IDX_TUI, FONT_IDX_GE,
    FONT_IDX_QU, FONT_IDX_XIAO,
};
static const unsigned char code scr_unlocked[] = {
    FONT_IDX_YI2, FONT_IDX_KAI, FONT_IDX_SUO,
};
static const unsigned char code scr_welcome_home[] = {
    FONT_IDX_HUAN, FONT_IDX_YING2, FONT_IDX_HUI, FONT_IDX_JIA,
};
static const unsigned char code scr_change_pwd[] = {
    FONT_IDX_XIU, FONT_IDX_GAI, FONT_IDX_MI, FONT_IDX_MA,
};
static const unsigned char code scr_relock[] = {
    FONT_IDX_CHONG, FONT_IDX_XIN, FONT_IDX_SHANG, FONT_IDX_SUO,
};
static const unsigned char code scr_new_pwd[] = {
    FONT_IDX_QING, FONT_IDX_SHU, FONT_IDX_RU, FONT_IDX_XIN,
    FONT_IDX_MI, FONT_IDX_MA,
};
static const unsigned char code scr_confirm_pwd[] = {
    FONT_IDX_QING, FONT_IDX_ZAI, FONT_IDX_CI, FONT_IDX_SHU,
    FONT_IDX_RU, FONT_IDX_MI, FONT_IDX_MA,
};
static const unsigned char code scr_pwd_error[] = {
    FONT_IDX_MI, FONT_IDX_MA, FONT_IDX_CUO, FONT_IDX_WU,
};
static const unsigned char code scr_remain_prefix[] = {
    FONT_IDX_HAI, FONT_IDX_SHENG,
};
static const unsigned char code scr_remain_suffix[] = {
    FONT_IDX_CI, FONT_IDX_JI, FONT_IDX_HUI2,
};
static const unsigned char code scr_pwd_ok[] = {
    FONT_IDX_MI, FONT_IDX_MA, FONT_IDX_XIU, FONT_IDX_GAI,
    FONT_IDX_CHENG, FONT_IDX_GONG,
};

static void show_locked(void)
{
    OLED_Clear();
    show_chinese(2, 32, scr_welcome, 4);
    show_chinese(4, 16, scr_prompt, 6);
}

static void show_input(void)
{
    OLED_Clear();
    show_chinese(0, 24, scr_enter_pwd, 5);
    show_cursor(0);
}

static void show_unlocked(void)
{
    OLED_Clear();
    show_chinese(1, 40, scr_unlocked, 3);
    show_chinese(4, 32, scr_welcome_home, 4);
}

static void show_setpwd(void)
{
    OLED_Clear();
    show_chinese(0, 16, scr_new_pwd, 6);
    show_cursor(0);
}

static void show_setconf(void)
{
    OLED_Clear();
    show_chinese(0, 8, scr_confirm_pwd, 7);
    show_cursor(0);
}

static void show_error(void)
{
    unsigned char remain;

    OLED_Clear();
    show_chinese(1, 32, scr_pwd_error, 4);

    // "还剩 X 次机会"
    if (error_cnt >= 5) remain = 0;
    else remain = 5 - error_cnt;

    show_chinese(4, 16, scr_remain_prefix, 2);                       // 还剩
    OLED_ShowImage_H16(4, 16 + 32, 16, font_data[FONT_IDX_D0 + remain]);  // 数字
    show_chinese(4, 16 + 48, scr_remain_suffix, 3);                  // 次机会
}

static void show_pwd_ok(void)
{
    OLED_Clear();
    show_chinese(2, 16, scr_pwd_ok, 6);
}

static const unsigned char code scr_pwd_mismatch[] = {
    FONT_IDX_MI, FONT_IDX_MA, FONT_IDX_QUE, FONT_IDX_REN2,
    FONT_IDX_CUO, FONT_IDX_WU,
};
static void show_pwd_mismatch(void)
{
    OLED_Clear();
    show_chinese(2, 16, scr_pwd_mismatch, 6);
}

static const unsigned char code scr_lockdown[] = {
    FONT_IDX_SUO, FONT_IDX_SI,
};

static void show_lockdown_countdown(unsigned char sec)
{
    unsigned char tens = sec / 10;
    unsigned char ones = sec % 10;
    OLED_ShowImage_H16(4, 32, 16, font_data[FONT_IDX_D0 + tens]);
    OLED_ShowImage_H16(4, 48, 16, font_data[FONT_IDX_D0 + ones]);
    OLED_ShowImage_H16(4, 64, 16, font_data[FONT_IDX_MIAO]);
}

static void show_lockdown(void)
{
    OLED_Clear();
    show_chinese(1, 48, scr_lockdown, 2);
    last_displayed_sec = 30;
    show_lockdown_countdown(30);
}

static void show_pairing(void)
{
    static const unsigned char code scr_pairing[] = {FONT_IDX_LAN, FONT_IDX_YA, FONT_IDX_PEI, FONT_IDX_DUI, FONT_IDX_DENG, FONT_IDX_DAI};
    OLED_Clear();
    show_chinese(2, 16, scr_pairing, 6);   // 蓝牙配对等待
}

static void show_pairing_ok(void)
{
    static const unsigned char code scr_pair_ok[] = {FONT_IDX_PEI, FONT_IDX_DUI, FONT_IDX_CHENG, FONT_IDX_GONG};
    OLED_Clear();
    show_chinese(2, 32, scr_pair_ok, 4);
}

// ============================================================
// 更新光标 (INPUT / SET_PWD / SET_CONF 共用)
// ============================================================
static void update_cursor(unsigned char count)
{
    show_cursor(count);
}

// ============================================================
// 状态超时检测 (每次 Task 调用)
// ============================================================
static void check_timeouts(void)
{
    unsigned long elapsed = uwtick - state_timer;

    if (state == STATE_UNLOCKED && elapsed > 15000)
    {
        state = STATE_LOCKED;
        show_locked();
    }
    else if (state == STATE_ERROR && elapsed > 3000)
    {
        state = STATE_LOCKED;
        show_locked();
    }
    else if (state == STATE_PWD_OK && elapsed > 2000)
    {
        state = STATE_UNLOCKED;
        state_timer = uwtick;
        show_unlocked();
    }
    else if (state == STATE_PWD_MISMATCH && elapsed > 3000)
    {
        state = STATE_UNLOCKED;
        state_timer = uwtick;
        show_unlocked();
    }
    else if (state == STATE_LOCK_DOWN)
    {
        unsigned char remaining = 30 - (unsigned char)(elapsed / 1000);
        if (elapsed >= 30000)
        {
            state = STATE_LOCKED;
            error_cnt = 0;
            show_locked();
        }
        else if (remaining != last_displayed_sec)
        {
            last_displayed_sec = remaining;
            show_lockdown_countdown(remaining);
        }
    }
    else if (state == STATE_PAIRING && elapsed > 30000)
    {
        state = STATE_UNLOCKED;
        state_timer = uwtick;
        show_unlocked();
    }
    else if (state == STATE_PAIRING_OK && elapsed > 3000)
    {
        state = STATE_UNLOCKED;
        state_timer = uwtick;
        show_unlocked();
    }
}

// ============================================================
// 按键处理
// ============================================================
static void handle_key(unsigned char key)
{
    unsigned char digit;
    unsigned char i;

    switch (state)
    {
    // ────────── 待机 ──────────
    case STATE_LOCKED:
        // 任意按键唤醒，进入输入模式，按键值不记入密码
        state = STATE_INPUT;
        pwd_idx = 0;
        show_input();
        break;

    // ────────── 密码输入 ──────────
    case STATE_INPUT:
        digit = key_to_digit[key];

        if (digit <= 9 && pwd_idx < PWD_LEN)
        {
            pwd_buf[pwd_idx++] = digit;
            update_cursor(pwd_idx);
            if (pwd_idx == PWD_LEN)
            {
                // 6 位满，自动验证
                if (check_password())
                {
                    state = STATE_UNLOCKED;
                    error_cnt = 0;
                    state_timer = uwtick;
                    show_unlocked();
                }
                else
                {
                    error_cnt++;
                    if (error_cnt >= 5)
                    {
                        state = STATE_LOCK_DOWN;
                        state_timer = uwtick;
                        show_lockdown();
                    }
                    else
                    {
                        state = STATE_ERROR;
                        state_timer = uwtick;
                        show_error();
                    }
                }
            }
        }
        else if (key == KEY_CONFIRM)
        {
            if (pwd_idx == PWD_LEN)
            {
                if (check_password())
                {
                    state = STATE_UNLOCKED;
                    error_cnt = 0;
                    state_timer = uwtick;
                    show_unlocked();
                }
                else
                {
                    error_cnt++;
                    if (error_cnt >= 5)
                    {
                        state = STATE_LOCK_DOWN;
                        state_timer = uwtick;
                        show_lockdown();
                    }
                    else
                    {
                        state = STATE_ERROR;
                        state_timer = uwtick;
                        show_error();
                    }
                }
            }
        }
        else if (key == KEY_BACK && pwd_idx > 0)
        {
            pwd_idx--;
            update_cursor(pwd_idx);
        }
        else if (key == KEY_CANCEL)
        {
            state = STATE_LOCKED;
            show_locked();
        }
        break;

    // ────────── 已开锁 ──────────
    case STATE_UNLOCKED:
        if (key == KEY_FUNC)
        {
            // 功能键(16) → 修改密码
            state = STATE_SET_PWD;
            new_pwd_idx = 0;
            show_setpwd();
        }
        else if (key == KEY_PAIR)
        {
            // # 键(15) → 蓝牙配对模式
            state = STATE_PAIRING;
            state_timer = uwtick;
            show_pairing();
            UART_SendString("ENTER_PAIR\n");
        }
        break;

    // ────────── 输入新密码 ──────────
    case STATE_SET_PWD:
        digit = key_to_digit[key];

        if (digit <= 9 && new_pwd_idx < PWD_LEN)
        {
            new_pwd[new_pwd_idx++] = digit;
            update_cursor(new_pwd_idx);
            if (new_pwd_idx == PWD_LEN)
            {
                state = STATE_SET_CONF;
                pwd_idx = 0;
                show_setconf();
            }
        }
        else if (key == KEY_BACK && new_pwd_idx > 0)
        {
            new_pwd_idx--;
            update_cursor(new_pwd_idx);
        }
        else if (key == KEY_CANCEL)
        {
            state = STATE_UNLOCKED;
            state_timer = uwtick;
            show_unlocked();
        }
        break;

    // ────────── 确认新密码 ──────────
    case STATE_SET_CONF:
        digit = key_to_digit[key];

        if (digit <= 9 && pwd_idx < PWD_LEN)
        {
            pwd_buf[pwd_idx++] = digit;
            update_cursor(pwd_idx);
            if (pwd_idx == PWD_LEN)
            {
                // 两次输入一致？
                for (i = 0; i < PWD_LEN; i++)
                {
                    if (pwd_buf[i] != new_pwd[i])
                        break;
                }
                if (i == PWD_LEN)
                {
                    // 一致 → 保存新密码
                    save_password(new_pwd);
                    for (i = 0; i < PWD_LEN; i++)
                        pwd_saved[i] = new_pwd[i];
                    state = STATE_PWD_OK;
                    state_timer = uwtick;
                    show_pwd_ok();
                }
                else
                {
                    // 不一致 → 显示提示，3秒后返回欢迎回家
                    state = STATE_PWD_MISMATCH;
                    state_timer = uwtick;
                    show_pwd_mismatch();
                }
            }
        }
        else if (key == KEY_BACK && pwd_idx > 0)
        {
            pwd_idx--;
            update_cursor(pwd_idx);
        }
        else if (key == KEY_CANCEL)
        {
            state = STATE_UNLOCKED;
            state_timer = uwtick;
            show_unlocked();
        }
        break;

    // ────────── 密码错误 / 修改成功 / 密码不一致 / 配对模式 ──────────
    case STATE_ERROR:
    case STATE_PWD_OK:
    case STATE_PWD_MISMATCH:
    case STATE_PAIRING:
    case STATE_PAIRING_OK:
        // 按键可提前返回 (超时等待期间)
        if (key == KEY_CONFIRM || key == KEY_CANCEL)
        {
            if (state == STATE_PWD_MISMATCH || state == STATE_PAIRING
                || state == STATE_PAIRING_OK)
            {
                state = STATE_UNLOCKED;
                state_timer = uwtick;
                show_unlocked();
            }
            else
            {
                state = STATE_LOCKED;
                show_locked();
            }
        }
        break;
    }
}

// ============================================================
// 初始化 (加载 EEPROM 密码)
// ============================================================
void MatrixKeyApp_Init(void)
{
    unsigned char i;

    // 上电时按住 KEY_FUNC(按键16) 可清除 EEPROM 恢复出厂密码 000000
    if (MatrixKey() == 16)
    {
        AT24C02_WriteByte(PWD_ADDR_MAGIC, 0x00);
        Delay(5);
        UART_SendString("EEPROM cleared.\r\n");
    }

    load_password();
    state = STATE_LOCKED;
    error_cnt = 0;
    show_locked();

    // 通过 UART 输出当前密码 (调试用)
    UART_SendString("PWD: ");
    for (i = 0; i < PWD_LEN; i++)
    {
        UART_SendByte(pwd_saved[i] + '0');
    }
    UART_SendString("\r\n");
}

// ============================================================
// 主任务 (10ms 周期)
// ============================================================
void MatrixKey_Task(void)
{
    Key_Val = MatrixKey();
    Key_Down = Key_Val & (Key_Old ^ Key_Val);
    Key_Up = ~Key_Val & (Key_Old ^ Key_Val);
    Key_Old = Key_Val;

    // 超时检测
    check_timeouts();

    // 按键事件
    if (Key_Down)
    {
        handle_key(Key_Val);
    }
}

// ============================================================
// 供 UART 命令使用的密码读写接口
// ============================================================
unsigned char MatrixKeyApp_GetPassword(unsigned char *out)
{
    unsigned char i;
    for (i = 0; i < PWD_LEN; i++)
        out[i] = pwd_saved[i];
    return 1;
}

unsigned char MatrixKeyApp_SetPassword(const unsigned char *in)
{
    unsigned char i;

    // 更新内存
    for (i = 0; i < PWD_LEN; i++)
        pwd_saved[i] = in[i];

    // 写入 AT24C02
    save_password(pwd_saved);

    return 1;
}

void MatrixKeyApp_OnPairOk(void)
{
    state = STATE_PAIRING_OK;
    state_timer = uwtick;
    show_pairing_ok();
}

void MatrixKeyApp_OnUnlock(void)
{
    // 配对流程中不覆盖 — PAIRING_OK 3 秒后会自己进 UNLOCKED
    if (state == STATE_PAIRING || state == STATE_PAIRING_OK)
        return;
    state = STATE_UNLOCKED;
    state_timer = uwtick;
    error_cnt = 0;
    show_unlocked();
}
