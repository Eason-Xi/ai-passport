// tests/test_yz_power.c —— 空闲调暗 / 熄屏、按键唤醒、计时中保持常亮、计数回绕。
#include "yz_power.h"
#include "yz_test.h"

int main(void) {
    yz_cfg_t cfg;
    yz_cfg_default(&cfg);
    yz_power_t p;
    yz_power_init(&p, 1000);
    CHECK(yz_power_backlight(&p, &cfg) == YZ_BRIGHT_PERCENT[cfg.bright_idx]);
    CHECK(!yz_power_tick(&p, 1000 + YZ_DIM_AFTER_MS - 1, false));
    CHECK(yz_power_tick(&p, 1000 + YZ_DIM_AFTER_MS, false) && p.level == YZ_POWER_DIM);
    CHECK(yz_power_backlight(&p, &cfg) == YZ_DIM_PERCENT);
    CHECK(yz_power_tick(&p, 1000 + YZ_OFF_AFTER_MS, false) && p.level == YZ_POWER_OFF);
    CHECK(yz_power_backlight(&p, &cfg) == 0);

    // 熄屏时第一下按键只唤醒；亮屏时按键照常处理。
    CHECK(yz_power_key(&p, 400000));
    CHECK(p.level == YZ_POWER_ON && !yz_power_key(&p, 400001));

    // 忙碌（计时临写 / 提示音）时不调暗，已调暗则立刻恢复。
    CHECK(!yz_power_tick(&p, 400001 + YZ_OFF_AFTER_MS, true));
    CHECK(!yz_power_tick(&p, 400001 + YZ_OFF_AFTER_MS + 10, false));
    p.level = YZ_POWER_DIM;
    CHECK(yz_power_tick(&p, 900000, true) && p.level == YZ_POWER_ON);

    // 32 位毫秒计数回绕不误判。
    yz_power_init(&p, 0xFFFFFF00u);
    CHECK(!yz_power_tick(&p, 0x00000100u, false));

    // 亮度设置低于调暗值时，调暗不会反而更亮。
    cfg.bright_idx = 0;
    p.level = YZ_POWER_DIM;
    CHECK(yz_power_backlight(&p, &cfg) <= YZ_BRIGHT_PERCENT[0]);
    printf("test_yz_power: PASS\n");
    return 0;
}
