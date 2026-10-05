// main/kj_bump.c —— 碰拳配对结算（纯 C，主机测试见 tests/test_kj_bump.c）。
#include "kj_bump.h"

#include <stdbool.h>
#include <string.h>

static uint32_t gap(uint32_t a, uint32_t b)
{
    int32_t d = (int32_t)(a - b);
    return d < 0 ? (uint32_t)(-(int64_t)d) : (uint32_t)d;
}

static void set_pair(const kj_bump_cand_t *c, kj_bump_out_t *out, int x, int y)
{
    uint32_t dt = gap(c[x].t, c[y].t);
    out[x].verdict = out[y].verdict = KJ_BUMP_PAIR;
    out[x].peer = c[y].idx;
    out[y].peer = c[x].idx;
    out[x].dt = out[y].dt = (uint16_t)(dt > 0xFFFF ? 0xFFFF : dt);
}

void kj_bump_resolve(const kj_bump_cand_t *c, int n, uint32_t now_ms, kj_bump_out_t *out)
{
    for (int i = 0; i < n; i++) memset(&out[i], 0, sizeof(out[i]));   // 全部先判 WAIT
    if (n > KJ_BUMP_MAX) n = KJ_BUMP_MAX;
    bool done[KJ_BUMP_MAX] = { false };
    for (;;) {
        // 尚未结算的人里按下最早的一位
        int a = -1;
        for (int i = 0; i < n; i++) {
            if (!done[i] && (a < 0 || (int32_t)(c[i].t - c[a].t) < 0)) a = i;
        }
        if (a < 0 || (int32_t)(now_ms - c[a].t) < KJ_BUMP_SETTLE_MS) return;

        // 这一簇：a、与 a 碰到的人（直接邻居），以及与直接邻居碰到的人。
        bool in[KJ_BUMP_MAX] = { false };
        bool direct[KJ_BUMP_MAX] = { false };
        int neighbors = 0, b = -1;
        in[a] = true;
        for (int j = 0; j < n; j++) {
            if (done[j] || j == a || gap(c[j].t, c[a].t) > KJ_BUMP_PAIR_MS) continue;
            in[j] = direct[j] = true;
            neighbors++;
            b = j;
        }
        if (neighbors == 0) {
            out[a].verdict = KJ_BUMP_ALONE;
            done[a] = true;
            continue;
        }
        int size = 1 + neighbors;
        for (int j = 0; j < n; j++) {
            if (!direct[j]) continue;
            for (int k = 0; k < n; k++) {
                if (done[k] || in[k] || gap(c[k].t, c[j].t) > KJ_BUMP_PAIR_MS) continue;
                in[k] = true;
                size++;
            }
        }
        if (size == 2) {
            set_pair(c, out, a, b);
            done[a] = done[b] = true;
            continue;
        }

        // 多人同时碰拳：时间最接近的两人足够近、且明显比别人近，才放行这一对。
        int x = -1, y = -1;
        uint32_t d1 = UINT32_MAX;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n && in[i]; j++) {
                if (!in[j]) continue;
                uint32_t d = gap(c[i].t, c[j].t);
                if (d < d1) {
                    d1 = d;
                    x = i;
                    y = j;
                }
            }
        }
        uint32_t d2 = UINT32_MAX;
        for (int z = 0; z < n; z++) {
            if (!in[z] || z == x || z == y) continue;
            uint32_t dx = gap(c[z].t, c[x].t), dy = gap(c[z].t, c[y].t);
            uint32_t d = dx < dy ? dx : dy;
            if (d < d2) d2 = d;
        }
        bool clear_pair = d1 <= KJ_BUMP_TIE_MS && d2 > 3u * d1;
        for (int i = 0; i < n; i++) {
            if (!in[i]) continue;
            done[i] = true;
            if (!clear_pair || (i != x && i != y)) out[i].verdict = KJ_BUMP_CROWD;
        }
        if (clear_pair) set_pair(c, out, x, y);
    }
}
