// tests/test_kj_bump.c —— 碰拳配对结算：一对、落单、多人拥挤、互为最近放行、结算时机、时间回绕。
#include "kj_bump.h"
#include "kj_test.h"

#include <string.h>

typedef struct {
    const char *name;
    int n;
    uint32_t t[8];          // 各人的按下时刻（idx = 下标 + 10，便于区分下标与编号）
    uint32_t now;
    uint8_t want[8];        // 期望的判定
    int8_t peer[8];         // PAIR 时期望的对方下标（-1 = 不检查）
} bump_case_t;

#define W KJ_BUMP_WAIT
#define P KJ_BUMP_PAIR
#define A KJ_BUMP_ALONE
#define C KJ_BUMP_CROWD

static const bump_case_t CASES[] = {
    { "pair", 2, { 1000, 1200 }, 1000 + KJ_BUMP_SETTLE_MS, { P, P }, { 1, 0 } },
    { "pair waits for settle time", 2, { 1000, 1200 }, 1000 + KJ_BUMP_SETTLE_MS - 1, { W, W }, { -1, -1 } },
    { "alone", 1, { 5000 }, 5000 + KJ_BUMP_SETTLE_MS, { A }, { -1 } },
    { "alone waits", 1, { 5000 }, 5100, { W }, { -1 } },
    { "pair limit inclusive", 2, { 1000, 1000 + KJ_BUMP_PAIR_MS }, 3000, { P, P }, { 1, 0 } },
    { "too far apart", 2, { 1000, 1000 + KJ_BUMP_PAIR_MS + 1 }, 3000, { A, A }, { -1, -1 } },
    // 第二人还没到结算时间：先把第一人判为落单，第二人继续等
    { "far apart, second still waiting", 2, { 1000, 1700 }, 1000 + KJ_BUMP_SETTLE_MS, { A, W }, { -1, -1 } },
    { "pair plus loner later", 3, { 1000, 1100, 3000 }, 1000 + KJ_BUMP_SETTLE_MS, { P, P, W }, { 1, 0, -1 } },
    { "two separate pairs", 4, { 1000, 1300, 5000, 5100 }, 6000, { P, P, P, P }, { 1, 0, 3, 2 } },
    // 三人几乎同时：分不清
    { "three at once", 3, { 1000, 1100, 1200 }, 3000, { C, C, C }, { -1, -1, -1 } },
    // 两人几乎同时（30 ms），第三人明显晚到（500 ms）：放行那一对，第三人判拥挤
    { "clear pair in crowd", 3, { 1000, 1030, 1530 }, 3000, { P, P, C }, { 1, 0, -1 } },
    // 最接近的两人相差 100 ms，第三人离他们 250 ms：不到 3 倍，整簇拥挤
    { "ambiguous crowd", 3, { 1000, 1100, 1350 }, 3000, { C, C, C }, { -1, -1, -1 } },
    // 链式：a-b 相邻、b-c 相邻、a-c 不相邻，仍算同一簇
    { "chained cluster", 3, { 1000, 1500, 2000 }, 4000, { C, C, C }, { -1, -1, -1 } },
    // 两人同一毫秒 + 远处的第三人：d1 = 0，第三人只要不是同一毫秒就放行
    { "exact tie", 3, { 1000, 1000, 1400 }, 4000, { P, P, C }, { 1, 0, -1 } },
    // 32 位时钟回绕
    { "wraparound pair", 2, { 0xFFFFFF00u, 0x00000010u }, 0x00000010u + KJ_BUMP_SETTLE_MS, { P, P }, { 1, 0 } },
    // 输入顺序与时间顺序无关
    { "unsorted input", 3, { 9000, 1000, 1250 }, 3000, { W, P, P }, { -1, 2, 1 } },
};

static void run_case(const bump_case_t *tc)
{
    kj_bump_cand_t cand[8];
    kj_bump_out_t out[8];
    for (int i = 0; i < tc->n; i++) {
        cand[i].idx = (uint8_t)(10 + i);
        cand[i].t = tc->t[i];
    }
    kj_bump_resolve(cand, tc->n, tc->now, out);
    for (int i = 0; i < tc->n; i++) {
        if (out[i].verdict != tc->want[i]) {
            fprintf(stderr, "case '%s': candidate %d verdict %u, want %u\n", tc->name, i, out[i].verdict,
                    tc->want[i]);
            kj_test_failures++;
        }
        if (tc->want[i] == KJ_BUMP_PAIR && tc->peer[i] >= 0 && out[i].peer != 10 + tc->peer[i]) {
            fprintf(stderr, "case '%s': candidate %d peer %u, want %d\n", tc->name, i, out[i].peer,
                    10 + tc->peer[i]);
            kj_test_failures++;
        }
    }
}

static void test_dt_and_overflow(void)
{
    kj_bump_cand_t cand[KJ_BUMP_MAX + 2];
    kj_bump_out_t out[KJ_BUMP_MAX + 2];
    cand[0] = (kj_bump_cand_t){ .idx = 3, .t = 100 };
    cand[1] = (kj_bump_cand_t){ .idx = 7, .t = 337 };
    kj_bump_resolve(cand, 2, 2000, out);
    CHECK_EQ(out[0].verdict, KJ_BUMP_PAIR);
    CHECK_EQ(out[0].peer, 7);
    CHECK_EQ(out[1].peer, 3);
    CHECK_EQ(out[0].dt, 237);
    CHECK_EQ(out[1].dt, 237);
    // 超出上限的候选者一律等待（不越界、不误判）
    for (int i = 0; i < KJ_BUMP_MAX + 2; i++) {
        cand[i].idx = (uint8_t)i;
        cand[i].t = 1000u + (uint32_t)i * 5000u;   // 彼此相距很远：前 KJ_BUMP_MAX 个都落单
    }
    memset(out, 0xEE, sizeof(out));
    kj_bump_resolve(cand, KJ_BUMP_MAX + 2, 1000u + 100000u, out);
    for (int i = 0; i < KJ_BUMP_MAX; i++) CHECK_EQ(out[i].verdict, KJ_BUMP_ALONE);
    CHECK_EQ(out[KJ_BUMP_MAX].verdict, KJ_BUMP_WAIT);
    CHECK_EQ(out[KJ_BUMP_MAX + 1].verdict, KJ_BUMP_WAIT);
    kj_bump_resolve(cand, 0, 0, out);   // 空输入
}

int main(void)
{
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) run_case(&CASES[i]);
    test_dt_and_overflow();
    KJ_TEST_DONE("test_kj_bump");
}
