/*
 * ==========================================================================
 *   역할 반전 블록깨기 v4.0 - "보스 블록을 사수하라!"
 * ==========================================================================
 *  [변경 내역 v4.0]
 *   - 아이템 4종 → 2종으로 축소
 *       (S) Shield : 보스 HP +1 회복  (파란색)
 *       (R) Repair : 내 블록 중 HP가 가장 낮은 것 +1 회복  (초록색)
 *   - 아이템 획득 시 하단에 설명 메시지 표시
 *   - 웨이브 종료마다 재배치 페이즈 (포인트 5점 지급)
 *
 *  [빌드]
 *   gcc -o block_defense block_defense_v4.c   (MinGW)
 *   cl  block_defense_v4.c /Fe:block_defense.exe  (MSVC)
 *
 *  [플랫폼]  Windows 전용
 * ==========================================================================
 */
#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#error "Windows only."
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

 /* ── 상수 ── */
#define ROWS          8
#define COLS          10
#define TOTAL_POINTS  15      /* 첫 배치 포인트 */
#define REPLACE_PTS   5       /* 웨이브 후 재배치 포인트 */
#define MAX_WAVES     5
#define BALL_STEPS    120
#define MAX_BALLS     3
#define MAX_ITEMS     8
#define SAVE_FILE     "save.dat"

/* 콘솔 레이아웃 */
#define GRID_TOP      7
#define GRID_LEFT     3
#define CELL_W        4
#define CELL_H        2
#define MSG_ROW       (GRID_TOP + ROWS * CELL_H + 1)

/* 블록 타입 */
#define EMPTY   0
#define NORMAL  1
#define BOSS    2

/* 아이템 타입 (2종만 사용) */
#define ITEM_SHIELD  1   /* 보스 HP +1 */
#define ITEM_REPAIR  2   /* 가장 HP 낮은 내 블록 +1 */

/* AI 패턴 */
#define AI_RANDOM    0
#define AI_SCAN      1
#define AI_FOCUS     2
#define AI_SPLIT     3
#define AI_BOSSRUSH  4

/* 색상 */
#define C_DEFAULT (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE)
#define C_WHITE   (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_GREEN   (FOREGROUND_GREEN|FOREGROUND_INTENSITY)
#define C_YELLOW  (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY)
#define C_RED     (FOREGROUND_RED|FOREGROUND_INTENSITY)
#define C_CYAN    (FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_MAGENTA (FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_BLUE    (FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_GRAY    (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE)
#define BG_HOVER  (BACKGROUND_RED|BACKGROUND_GREEN)

/* ── 구조체 ── */
typedef struct { int type, hp, maxHp; } Block;

typedef struct {
    double x, y, dx, dy;
    int    active;
    double speed;
} Ball;

typedef struct { int type, col, row, active; } Item;

#define SAVE_MAGIC 0xBD40
typedef struct {
    unsigned short magic;
    int score, wave, shieldActive;
    Block map[ROWS][COLS];
} SaveData;

/* ── 더블버퍼 ── */
#define BUF_W 80
#define BUF_H 40
static CHAR_INFO g_buf[BUF_H][BUF_W];

/* ── 전역 변수 ── */
static Block g_map[ROWS][COLS];
static Ball  g_balls[MAX_BALLS];
static Item  g_items[MAX_ITEMS];

static int g_points, g_wave, g_gameOver, g_score;
static int g_shieldActive, g_activeBallCount;
static int g_hoverRow, g_hoverCol;
static DWORD g_prevBtn;

static HANDLE g_hOut, g_hIn;

/* ==========================================================
   더블버퍼
========================================================== */
static void bufClear(void) {
    int r, c;
    for (r = 0; r < BUF_H; r++)
        for (c = 0; c < BUF_W; c++) {
            g_buf[r][c].Char.AsciiChar = ' ';
            g_buf[r][c].Attributes = (WORD)C_DEFAULT;
        }
}

static void bufPrint(int row, int col, const char* s, WORD attr) {
    int i;
    if (row < 0 || row >= BUF_H) return;
    for (i = 0; s[i] != '\0' && (col + i) < BUF_W; i++) {
        g_buf[row][col + i].Char.AsciiChar = s[i];
        g_buf[row][col + i].Attributes = attr;
    }
}

static void bufFlush(void) {
    SMALL_RECT sr = { 0, 0, BUF_W - 1, BUF_H - 1 };
    COORD      size = { BUF_W, BUF_H };
    COORD      orig = { 0, 0 };
    WriteConsoleOutputA(g_hOut, (CHAR_INFO*)g_buf, size, orig, &sr);
}

static void int2str(int v, char* buf) {
    char tmp[16]; int i = 0, neg = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (v < 0) { neg = 1; v = -v; }
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    { int j; for (j = 0;j < i;j++) buf[j] = tmp[i - 1 - j]; }
    buf[i] = '\0';
}

static void bufPrintInt(int row, int col, int v, WORD attr) {
    char s[16]; int2str(v, s); bufPrint(row, col, s, attr);
}

/* ==========================================================
   초기화
========================================================== */
static void initMap(void) {
    int r, c;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            g_map[r][c].type = g_map[r][c].hp = g_map[r][c].maxHp = 0;
    g_map[ROWS - 1][COLS / 2].type = BOSS;
    g_map[ROWS - 1][COLS / 2].hp = 5;
    g_map[ROWS - 1][COLS / 2].maxHp = 5;
}

static void initBalls(void) {
    int i;
    for (i = 0; i < MAX_BALLS; i++) g_balls[i].active = 0;
    g_activeBallCount = 0;
}

static void initItems(void) {
    int i;
    for (i = 0; i < MAX_ITEMS; i++) g_items[i].active = 0;
}

/* ==========================================================
   저장 / 불러오기
========================================================== */
static int saveGame(void) {
    FILE* f = fopen(SAVE_FILE, "wb");
    SaveData sd;
    if (!f) return 0;
    sd.magic = SAVE_MAGIC;
    sd.score = g_score;
    sd.wave = g_wave;
    sd.shieldActive = g_shieldActive;
    memcpy(sd.map, g_map, sizeof(g_map));
    fwrite(&sd, sizeof(sd), 1, f);
    fclose(f);
    return 1;
}

static int loadGame(void) {
    FILE* f = fopen(SAVE_FILE, "rb");
    SaveData sd;
    if (!f) return 0;
    if (fread(&sd, sizeof(sd), 1, f) != 1 || sd.magic != SAVE_MAGIC)
    {
        fclose(f); return 0;
    }
    g_score = sd.score;
    g_wave = sd.wave;
    g_shieldActive = sd.shieldActive;
    memcpy(g_map, sd.map, sizeof(g_map));
    fclose(f);
    return 1;
}

/* ==========================================================
   렌더링 헬퍼
========================================================== */
static WORD hpColor(int hp, int maxHp) {
    double r;
    if (maxHp <= 0) return (WORD)C_GRAY;
    r = (double)hp / (double)maxHp;
    if (r > 0.6) return (WORD)C_GREEN;
    if (r > 0.3) return (WORD)C_YELLOW;
    return (WORD)C_RED;
}

static void getBlockDisplay(Block b, int isHover, char out[5], WORD* attr) {
    if (isHover) {
        *attr = (WORD)(C_WHITE | BG_HOVER);
        out[0] = '['; out[1] = '_'; out[2] = ']'; out[3] = ' '; out[4] = '\0';
        return;
    }
    *attr = (WORD)C_DEFAULT;
    switch (b.type) {
    case EMPTY:
        out[0] = ' '; out[1] = '.'; out[2] = ' '; out[3] = ' '; out[4] = '\0';
        break;
    case NORMAL: {
        char num[4]; int2str(b.hp, num);
        out[0] = '['; out[1] = num[0]; out[2] = ']'; out[3] = ' '; out[4] = '\0';
        *attr = hpColor(b.hp, b.maxHp);
        break;
    }
    case BOSS:
        out[0] = '['; out[1] = 'B'; out[2] = ']'; out[3] = ' '; out[4] = '\0';
        *attr = (WORD)(b.hp >= 4 ? (C_MAGENTA | FOREGROUND_INTENSITY) :
            b.hp >= 2 ? C_MAGENTA : C_RED);
        break;
    default:
        out[0] = ' '; out[1] = '.'; out[2] = ' '; out[3] = ' '; out[4] = '\0';
        break;
    }
}

static void makeHpBar(int hp, int maxHp, int w, char* out) {
    int i, filled;
    if (maxHp <= 0) { out[0] = '\0'; return; }
    filled = (int)((double)hp / (double)maxHp * (double)w + 0.5);
    out[0] = '[';
    for (i = 0;i < w;i++) out[1 + i] = (i < filled) ? '=' : ' ';
    out[1 + w] = ']'; out[2 + w] = '\0';
}

static void drawSeparator(int row) {
    char line[BUF_W + 1]; int j;
    for (j = 0;j < BUF_W - 1;j++) line[j] = '-';
    line[BUF_W - 1] = '\0';
    bufPrint(row, 0, line, (WORD)C_GRAY);
}

/* ==========================================================
   공통 그리드 그리기
========================================================== */
static void drawGrid(int phase) {
    int r, c;
    for (r = 0; r < ROWS; r++) {
        char rn[4]; int2str(r, rn);
        bufPrint(GRID_TOP + r * CELL_H, GRID_LEFT - 2, rn, (WORD)C_GRAY);

        for (c = 0; c < COLS; c++) {
            int  isHover = (phase == 0
                && r == g_hoverRow && c == g_hoverCol
                && g_map[r][c].type == EMPTY
                && r > 0 && r < ROWS - 1);
            char cell[5]; WORD attr;
            getBlockDisplay(g_map[r][c], isHover, cell, &attr);

            /* 전투 중: 아이템/볼 덮어쓰기 */
            if (phase == 1) {
                int ii;
                for (ii = 0; ii < MAX_ITEMS; ii++) {
                    if (!g_items[ii].active) continue;
                    if (g_items[ii].row == r && g_items[ii].col == c) {
                        if (g_items[ii].type == ITEM_SHIELD)
                        {
                            cell[0] = '(';cell[1] = 'S';cell[2] = ')';cell[3] = ' ';cell[4] = '\0'; attr = (WORD)C_BLUE;
                        }
                        else
                        {
                            cell[0] = '(';cell[1] = 'R';cell[2] = ')';cell[3] = ' ';cell[4] = '\0'; attr = (WORD)C_GREEN;
                        }
                        break;
                    }
                }
                {
                    int bi;
                    for (bi = 0; bi < MAX_BALLS; bi++) {
                        if (!g_balls[bi].active) continue;
                        if ((int)(g_balls[bi].y + 0.5) == r && (int)(g_balls[bi].x + 0.5) == c) {
                            cell[0] = '(';cell[1] = 'o';cell[2] = ')';cell[3] = ' ';cell[4] = '\0';
                            attr = (WORD)C_WHITE; break;
                        }
                    }
                }
            }
            bufPrint(GRID_TOP + r * CELL_H, GRID_LEFT + c * CELL_W, cell, attr);
        }
    }
}

/* ==========================================================
   범례 (아이템 2종만 표시 + 설명)
========================================================== */
static void drawLegend(int row) {
    bufPrint(row, 2, "Blocks:", (WORD)C_WHITE);
    bufPrint(row, 10, "[1]Block(HP1)", (WORD)C_GREEN);
    bufPrint(row, 24, "[B]Boss", (WORD)C_MAGENTA);

    bufPrint(row + 1, 2, "Items :", (WORD)C_WHITE);
    /* (S) Shield */
    bufPrint(row + 1, 10, "(S)", (WORD)C_BLUE);
    bufPrint(row + 1, 13, "Shield : Boss HP +1", (WORD)C_GRAY);
    /* (R) Repair */
    bufPrint(row + 1, 34, "(R)", (WORD)C_GREEN);
    bufPrint(row + 1, 37, "Repair : Weakest block HP +1", (WORD)C_GRAY);
}

/* ==========================================================
   배치 UI 패널
========================================================== */
static void drawPlacementUI(int isReplace) {
    char tmp[64]; int i;

    if (isReplace)
        bufPrint(0, 1, "=== BLOCK DEFENSE v4.0  [RE-PLACE after wave] ===", (WORD)C_YELLOW);
    else
        bufPrint(0, 1, "=== BLOCK DEFENSE v4.0  [PLACEMENT] ===", (WORD)C_WHITE);

    bufPrint(1, 2, "Points: ", (WORD)C_YELLOW);
    bufPrintInt(1, 10, g_points, (WORD)C_YELLOW);
    bufPrint(1, 12, "/", (WORD)C_YELLOW);
    bufPrintInt(1, 13, isReplace ? REPLACE_PTS : TOTAL_POINTS, (WORD)C_YELLOW);

    if (isReplace) {
        /* 재배치: 웨이브/점수/보스HP 추가 표시 */
        bufPrint(1, 20, "Wave:", (WORD)C_CYAN);
        bufPrintInt(1, 26, g_wave, (WORD)C_CYAN);
        bufPrint(1, 28, "/5", (WORD)C_CYAN);
        bufPrint(1, 32, "Score:", (WORD)C_YELLOW);
        bufPrintInt(1, 39, g_score, (WORD)C_YELLOW);
        char bar[20];
        makeHpBar(g_map[ROWS - 1][COLS / 2].hp, 5, 8, bar);
        bufPrint(1, 47, "Boss:", (WORD)C_WHITE);
        bufPrint(1, 53, bar, hpColor(g_map[ROWS - 1][COLS / 2].hp, 5));
    }

    bufPrint(2, 2, "[ ENTER = Done ]", (WORD)C_GREEN);
    bufPrint(2, 20, "[ S = Save ]", (WORD)C_CYAN);
    bufPrint(2, 33, "[ L = Load ]", (WORD)C_CYAN);
    bufPrint(3, 2,
        "LClick:place  RClick:remove  Enter:start",
        (WORD)C_GRAY);
    drawSeparator(4);

    /* 열 번호 헤더 */
    for (i = 0; i < COLS; i++) {
        tmp[0] = (char)('0' + i); tmp[1] = ' '; tmp[2] = ' '; tmp[3] = '\0';
        bufPrint(5, GRID_LEFT + i * CELL_W, tmp, (WORD)C_GRAY);
    }
    (void)tmp;
}

/* ==========================================================
   전투 UI 패널
========================================================== */
static void drawBattleUI(void) {
    char bar[20];
    int  bossHp = g_map[ROWS - 1][COLS / 2].hp;

    bufPrint(0, 1, "=== BLOCK DEFENSE v4.0  [BATTLE] ===", (WORD)C_WHITE);

    bufPrint(1, 2, "Wave: ", (WORD)C_YELLOW);
    bufPrintInt(1, 8, g_wave, (WORD)C_YELLOW);
    bufPrint(1, 10, "/", (WORD)C_YELLOW);
    bufPrintInt(1, 11, MAX_WAVES, (WORD)C_YELLOW);

    bufPrint(1, 20, "Score: ", (WORD)C_YELLOW);
    bufPrintInt(1, 27, g_score, (WORD)C_YELLOW);

    makeHpBar(bossHp, 5, 10, bar);
    bufPrint(2, 2, "Boss HP: ", (WORD)C_WHITE);
    bufPrint(2, 11, bar, hpColor(bossHp, 5));
    bufPrintInt(2, 24, bossHp, hpColor(bossHp, 5));
    bufPrint(2, 26, "/5", (WORD)C_WHITE);

    if (g_shieldActive > 0) {
        bufPrint(2, 32, "[SHIELD:", (WORD)C_BLUE);
        bufPrintInt(2, 40, g_shieldActive, (WORD)C_BLUE);
        bufPrint(2, 41, "]", (WORD)C_BLUE);
    }

    bufPrint(3, 2, "Balls: ", (WORD)C_GRAY);
    bufPrintInt(3, 9, g_activeBallCount, (WORD)C_GRAY);

    drawSeparator(4);
}

/* 렌더 래퍼 */
static void renderPlacement(int isReplace) {
    bufClear();
    drawPlacementUI(isReplace);
    drawGrid(0);
    drawLegend(GRID_TOP + ROWS * CELL_H + 1);
    bufFlush();
}

static void renderBattle(void) {
    bufClear();
    drawBattleUI();
    drawGrid(1);
    drawLegend(GRID_TOP + ROWS * CELL_H + 1);
    bufFlush();
}

/* 메시지 표시 */
static void showMsg(const char* msg, WORD attr, int ms) {
    char blank[BUF_W]; memset(blank, ' ', BUF_W - 1); blank[BUF_W - 1] = '\0';
    bufPrint(MSG_ROW, 0, blank, (WORD)C_DEFAULT);
    bufPrint(MSG_ROW, 2, msg, attr);
    bufFlush();
    Sleep((DWORD)ms);
}

/* ==========================================================
   마우스 좌표 변환
========================================================== */
static int mouseToGrid(int mx, int my, int* outR, int* outC) {
    int c = (mx - GRID_LEFT) / CELL_W;
    int r = (my - GRID_TOP) / CELL_H;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    *outR = r; *outC = c; return 1;
}

/* 반환: 10=완료, 11=저장, 12=로드, 0=없음 */
static int mouseToButton(int mx, int my) {
    if (my == 2) {
        if (mx >= 2 && mx <= 17) return 10;
        if (mx >= 20 && mx <= 31) return 11;
        if (mx >= 33 && mx <= 44) return 12;
    }
    return 0;
}

/* ==========================================================
   배치/재배치 공통 함수
========================================================== */
static void runPlacement(int isReplace) {
    INPUT_RECORD ir; DWORD numRead;
    int done = 0, gr, gc;

    g_hoverRow = -1; g_hoverCol = -1;
    g_prevBtn = 0;

    SetConsoleMode(g_hIn, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    while (!done) {
        renderPlacement(isReplace);
        ReadConsoleInput(g_hIn, &ir, 1, &numRead);

        if (ir.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD* me = &ir.Event.MouseEvent;
            int mx = (int)me->dwMousePosition.X;
            int my = (int)me->dwMousePosition.Y;
            DWORD cur = me->dwButtonState;
            DWORD pressed = cur & ~g_prevBtn;

            if (me->dwEventFlags & MOUSE_MOVED) {
                g_hoverRow = -1; g_hoverCol = -1;
                mouseToGrid(mx, my, &g_hoverRow, &g_hoverCol);
            }

            /* 좌클릭 (엣지 감지) */
            if (pressed & FROM_LEFT_1ST_BUTTON_PRESSED) {
                int btn = mouseToButton(mx, my);
                if (btn == 10) { done = 1; }
                else if (btn == 11) {
                    showMsg(saveGame() ? ">> Saved!" : ">> Save failed.", (WORD)C_GREEN, 700);
                }
                else if (btn == 12) {
                    showMsg(loadGame() ? ">> Loaded!" : ">> No save file.", (WORD)C_GREEN, 700);
                }
                else if (mouseToGrid(mx, my, &gr, &gc)) {
                    if (gr > 0 && gr < ROWS - 1 && g_map[gr][gc].type == EMPTY) {
                        if (g_points >= 1) {
                            g_map[gr][gc].type = NORMAL;
                            g_map[gr][gc].hp = 1;
                            g_map[gr][gc].maxHp = 1;
                            g_points--;
                        }
                        else {
                            showMsg(">> Not enough points!", (WORD)C_RED, 600);
                        }
                    }
                }
            }

            /* 우클릭 (엣지 감지) */
            if (pressed & RIGHTMOST_BUTTON_PRESSED) {
                if (mouseToGrid(mx, my, &gr, &gc)) {
                    if (gr > 0 && gr < ROWS - 1 && g_map[gr][gc].type == NORMAL) {
                        g_points++;
                        g_map[gr][gc].type = g_map[gr][gc].hp = g_map[gr][gc].maxHp = 0;
                    }
                }
            }
            g_prevBtn = cur;
        }

        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            char k = ir.Event.KeyEvent.uChar.AsciiChar;
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            if (k == 's' || k == 'S')
                showMsg(saveGame() ? ">> Saved!" : ">> Save failed.", (WORD)C_GREEN, 700);
            else if (k == 'l' || k == 'L')
                showMsg(loadGame() ? ">> Loaded!" : ">> No save file.", (WORD)C_GREEN, 700);
            else if (vk == VK_RETURN) done = 1;
        }
    }
}

/* 첫 배치 */
static void placementPhase(void) {
    g_points = TOTAL_POINTS;
    runPlacement(0);
}

/* 웨이브 후 재배치 */
static void replacementPhase(void) {
    g_points = REPLACE_PTS;
    showMsg(">> Wave clear! Re-place your blocks! (5pts)", (WORD)C_YELLOW, 1200);
    runPlacement(1);
}

/* ==========================================================
   AI
========================================================== */
static int colScore(int col) {
    int r, s = 0;
    for (r = 0;r < ROWS;r++) s += g_map[r][col].hp;
    return s;
}
static int weakestCol(void) {
    int c, ms = 99999, wc = 0;
    for (c = 0;c < COLS;c++) { int s = colScore(c); if (s < ms) { ms = s;wc = c; } }
    return wc;
}
static int getAIPattern(int w) {
    if (w <= 1) return AI_RANDOM;
    if (w == 2) return AI_SCAN;
    if (w == 3) return AI_FOCUS;
    if (w == 4) return AI_SPLIT;
    return AI_BOSSRUSH;
}
static int aiBallCount(int p) {
    return p == AI_FOCUS ? 2 : p == AI_SPLIT ? 3 : p == AI_BOSSRUSH ? 2 : 1;
}
static int aiTargetCol(int pat, int bi) {
    int wk = weakestCol(), t;
    switch (pat) {
    case AI_RANDOM:   return rand() % COLS;
    case AI_SCAN:     return (rand() % 3 == 0) ? wk : rand() % COLS;
    case AI_FOCUS:    t = wk + (rand() % 3) - 1; return t < 0 ? 0 : t >= COLS ? COLS - 1 : t;
    case AI_SPLIT: { int a[3] = { wk,COLS / 4,COLS * 3 / 4 }; return a[bi % 3]; }
    case AI_BOSSRUSH: return COLS / 2;
    default:          return rand() % COLS;
    }
}

/* ==========================================================
   아이템 (2종)
========================================================== */
static void dropItem(int row, int col) {
    int i;
    if (rand() % 10 >= 4) return;   /* 40% 확률로 드롭 */
    for (i = 0;i < MAX_ITEMS;i++) {
        if (!g_items[i].active) {
            g_items[i].active = 1;
            g_items[i].row = row;
            g_items[i].col = col;
            /* Shield / Repair 50:50 */
            g_items[i].type = (rand() % 2 == 0) ? ITEM_SHIELD : ITEM_REPAIR;
            return;
        }
    }
}

static void applyItem(int idx) {
    switch (g_items[idx].type) {

    case ITEM_SHIELD:
        /* 보스 HP +1 (최대 5) */
        if (g_map[ROWS - 1][COLS / 2].hp < 5)
            g_map[ROWS - 1][COLS / 2].hp++;
        g_shieldActive = 1;
        /* ── 획득 메시지 ── */
        showMsg(
            ">> Item: (S) SHIELD — Boss HP +1!",
            (WORD)C_BLUE, 900);
        break;

    case ITEM_REPAIR: {
        /* HP가 가장 낮은 내 블록을 찾아 +1 */
        int r, c, br = -1, bc = -1, minHp = 99999;
        for (r = 1; r < ROWS - 1; r++)
            for (c = 0; c < COLS; c++)
                if (g_map[r][c].type == NORMAL && g_map[r][c].hp < minHp) {
                    minHp = g_map[r][c].hp; br = r; bc = c;
                }
        if (br != -1) {
            g_map[br][bc].hp++;
            if (g_map[br][bc].hp > g_map[br][bc].maxHp)
                g_map[br][bc].maxHp = g_map[br][bc].hp;
            /* ── 획득 메시지 ── */
            showMsg(
                ">> Item: (R) REPAIR — Weakest block HP +1!",
                (WORD)C_GREEN, 900);
        }
        else {
            g_score += 20;
            showMsg(
                ">> Item: (R) REPAIR — No block to repair. Score +20",
                (WORD)C_GREEN, 900);
        }
        break;
    }

    default: break;
    }
    g_items[idx].active = 0;
}

static void checkItems(int bx, int by) {
    int i;
    for (i = 0;i < MAX_ITEMS;i++)
        if (g_items[i].active && g_items[i].col == bx && g_items[i].row == by)
            applyItem(i);
}

/* ==========================================================
   볼 물리
========================================================== */
static void hitBlock(int r, int c) {
    int bi;
    if (g_map[r][c].type == EMPTY) return;

    for (bi = 0;bi < MAX_BALLS;bi++)
        if (g_balls[bi].active) g_balls[bi].dy = -g_balls[bi].dy;

    g_map[r][c].hp--;
    g_score += 10;

    if (g_map[r][c].type == BOSS) {
        if (g_shieldActive > 0) { g_map[r][c].hp++; g_shieldActive--; }
        if (g_map[r][c].hp <= 0) g_gameOver = -1;
        return;
    }
    if (g_map[r][c].hp <= 0) {
        dropItem(r, c);
        g_map[r][c].type = g_map[r][c].hp = g_map[r][c].maxHp = 0;
    }
}

static void moveSingleBall(int bi) {
    Ball* b = &g_balls[bi];
    int bx, by;
    b->x += b->dx * b->speed;
    b->y += b->dy * b->speed;
    if (b->x < 0) { b->x = 0;      b->dx = -b->dx; }
    if (b->x >= COLS) { b->x = COLS - 1; b->dx = -b->dx; }
    if (b->y < 0) { b->y = 0;      b->dy = -b->dy; }
    if (b->y >= ROWS) { b->active = 0; g_activeBallCount--; return; }
    bx = (int)(b->x + 0.5); by = (int)(b->y + 0.5);
    if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
        checkItems(bx, by);
        if (g_map[by][bx].type != EMPTY) hitBlock(by, bx);
    }
}

/* ==========================================================
   전투 페이즈 (웨이브마다 끝나면 재배치)
========================================================== */
static void battlePhase(void) {
    int w, step, bi;
    SetConsoleMode(g_hIn, 0);

    for (w = 1; w <= MAX_WAVES; w++) {
        int pat, cnt, tc;
        char waveMsg[48];

        g_wave = w;
        if (g_gameOver) break;

        pat = getAIPattern(w);
        cnt = aiBallCount(pat);

        initBalls();
        initItems();

        for (bi = 0; bi < cnt && bi < MAX_BALLS; bi++) {
            tc = aiTargetCol(pat, bi);
            if (tc < 0) tc = 0; if (tc >= COLS) tc = COLS - 1;
            g_balls[bi].x = (double)tc;
            g_balls[bi].y = 0.0;
            g_balls[bi].dx = (rand() % 2) ? 0.5 : -0.5;
            g_balls[bi].dy = 1.0;
            g_balls[bi].active = 1;
            g_balls[bi].speed = 1.0;
        }
        g_activeBallCount = cnt;

        switch (pat) {
        case AI_RANDOM:   strcpy(waveMsg, "Wave  : Random attack");          break;
        case AI_SCAN:     strcpy(waveMsg, "Wave  : Scanning weak point..."); break;
        case AI_FOCUS:    strcpy(waveMsg, "Wave  : Focused attack! (x2)");   break;
        case AI_SPLIT:    strcpy(waveMsg, "Wave  : SPLIT ATTACK!   (x3)");   break;
        case AI_BOSSRUSH: strcpy(waveMsg, "Wave  : BOSS RUSH!!!    (x2)");   break;
        default:          strcpy(waveMsg, "Wave  : ...");                    break;
        }
        waveMsg[5] = (char)('0' + w);

        renderBattle();
        showMsg(waveMsg, (WORD)C_WHITE, 1000);

        for (step = 0; step < BALL_STEPS && g_activeBallCount>0 && !g_gameOver; step++) {
            for (bi = 0;bi < MAX_BALLS;bi++)
                if (g_balls[bi].active) moveSingleBall(bi);
            renderBattle();
            Sleep(100);
        }

        for (bi = 0;bi < MAX_BALLS;bi++) g_balls[bi].active = 0;
        g_activeBallCount = 0;
        initItems();   /* 남은 아이템 정리 */

        if (g_gameOver == -1) break;
        if (g_shieldActive > 0) g_shieldActive--;

        renderBattle();
        Sleep(600);

        /* ── 웨이브 후 재배치 (마지막 웨이브 제외) ── */
        if (w < MAX_WAVES && !g_gameOver) {
            replacementPhase();
            SetConsoleMode(g_hIn, 0);  /* 재배치 후 다시 입력 비활성 */
        }
    }

    if (!g_gameOver) {
        g_gameOver = 1;
        g_score += g_map[ROWS - 1][COLS / 2].hp * 50;
    }
}

/* ==========================================================
   결과 화면
========================================================== */
static void showResult(void) {
    INPUT_RECORD ir; DWORD n;
    bufClear();
    bufPrint(2, 4, "+================================+", (WORD)C_WHITE);
    if (g_gameOver == -1) {
        bufPrint(3, 4, "|  !! GAME OVER                  |", (WORD)C_RED);
        bufPrint(4, 4, "|  Boss was destroyed...         |", (WORD)C_RED);
    }
    else {
        bufPrint(3, 4, "|  ** CLEAR! Boss survived! **   |", (WORD)C_GREEN);
        bufPrint(4, 4, "|                                |", (WORD)C_WHITE);
    }
    bufPrint(5, 4, "+================================+", (WORD)C_WHITE);
    bufPrint(6, 4, "|  Final Score  : ", (WORD)C_YELLOW);
    bufPrintInt(6, 22, g_score, (WORD)C_YELLOW);
    bufPrint(6, 28, "          |", (WORD)C_YELLOW);
    bufPrint(7, 4, "|  Wave reached : ", (WORD)C_WHITE);
    bufPrintInt(7, 22, g_wave, (WORD)C_WHITE);
    bufPrint(7, 23, " / 5            |", (WORD)C_WHITE);
    bufPrint(8, 4, "|  Boss HP left : ", (WORD)C_WHITE);
    bufPrintInt(8, 22, g_map[ROWS - 1][COLS / 2].hp, (WORD)C_WHITE);
    bufPrint(8, 23, " / 5            |", (WORD)C_WHITE);
    bufPrint(9, 4, "+================================+", (WORD)C_WHITE);
    bufPrint(11, 4, "Press any key to exit...", (WORD)C_GRAY);
    bufFlush();

    SetConsoleMode(g_hIn, 0);
    do { ReadConsoleInput(g_hIn, &ir, 1, &n); } while (!(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown));
}

/* ==========================================================
   메인
========================================================== */
int main(void) {
    CONSOLE_CURSOR_INFO ci;
    SMALL_RECT wr = { 0, 0, BUF_W - 1, BUF_H - 1 };
    COORD      cs = { BUF_W, BUF_H };
    DWORD      outMode = 0;

    srand((unsigned int)time(NULL));
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(g_hOut, &outMode);
    SetConsoleMode(g_hOut,
        outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);

    ci.dwSize = 1; ci.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &ci);
    SetConsoleScreenBufferSize(g_hOut, cs);
    SetConsoleWindowInfo(g_hOut, TRUE, &wr);

    initMap(); initBalls(); initItems();
    g_gameOver = g_score = g_wave = g_shieldActive = 0;

    /* 타이틀 */
    bufClear();
    bufPrint(1, 2, "+=============================================+", (WORD)C_WHITE);
    bufPrint(2, 2, "|  BLOCK DEFENSE v4.0 - Guard the Boss!      |", (WORD)C_WHITE);
    bufPrint(3, 2, "+=============================================+", (WORD)C_WHITE);
    bufPrint(4, 2, "|  LClick=place block   RClick=remove        |", (WORD)C_GREEN);
    bufPrint(5, 2, "|  Enter=start   S=save   L=load             |", (WORD)C_CYAN);
    bufPrint(6, 2, "+=============================================+", (WORD)C_WHITE);
    bufPrint(7, 2, "|  Items: (S)Shield=BossHP+1                 |", (WORD)C_BLUE);
    bufPrint(8, 2, "|         (R)Repair=WeakestBlockHP+1         |", (WORD)C_GREEN);
    bufPrint(9, 2, "|  After each wave: re-place blocks! (+5pts) |", (WORD)C_YELLOW);
    bufPrint(10, 2, "+=============================================+", (WORD)C_WHITE);
    bufPrint(12, 2, "  Press any key to start...", (WORD)C_GRAY);
    bufFlush();

    {
        INPUT_RECORD ir; DWORD n;
        SetConsoleMode(g_hIn, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
        do { ReadConsoleInput(g_hIn, &ir, 1, &n); } while (!(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown));
    }

    placementPhase();
    battlePhase();
    showResult();

    ci.dwSize = 25; ci.bVisible = TRUE;
    SetConsoleCursorInfo(g_hOut, &ci);
    return 0;
}