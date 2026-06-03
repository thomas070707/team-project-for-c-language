/*
 * ==========================================================================
 *   역할 반전 블록깨기 v3.1 - "보스 블록을 사수하라!"
 * ==========================================================================
 *  [수정 내역 v3.1]
 *   - bufPrint() 호출 인자 누락(col 빠짐) 전수 수정
 *   - 한국어 문자열 리터럴 → ASCII 영문으로 통일 (MSVC char* 경고 제거)
 *   - C4047/C2198 간접참조 불일치 완전 해소
 *   - getchar() 반환값 무시 경고 제거
 *   - MSVC /W4 기준 경고 0건 목표
 *
 *  [빌드]
 *   gcc -o block_defense block_defense.c -lm          (MinGW)
 *   cl  block_defense.c /W3 /Fe:block_defense.exe     (MSVC)
 *
 *  [플랫폼]  Windows 전용 (Windows Console API 사용)
 * ==========================================================================
 */

#ifndef _WIN32
#error "Windows only. Compile with MinGW or MSVC."
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma warning(disable: 4996)   /* MSVC: strcpy/strcat 안전 경고 무시 */

/* ── 상수 ── */
#define ROWS          8
#define COLS          10
#define TOTAL_POINTS  15
#define MAX_WAVES     5
#define BALL_STEPS    80
#define MAX_BALLS     3
#define MAX_ITEMS     8
#define SAVE_FILE     "save.dat"

/* 콘솔 레이아웃 */
#define UI_TOP        1
#define GRID_TOP      7
#define GRID_LEFT     3
#define CELL_W        4
#define CELL_H        2
#define MSG_ROW       (GRID_TOP + ROWS * CELL_H + 1)

/* 블록 타입 */
#define EMPTY   0
#define NORMAL  1
#define HARD    2
#define MIRROR  3
#define BOSS    4

/* 아이템 타입 */
#define ITEM_SHIELD    1
#define ITEM_MULTIBALL 2
#define ITEM_SLOW      3
#define ITEM_REPAIR    4

/* AI 패턴 */
#define AI_RANDOM    0
#define AI_SCAN      1
#define AI_FOCUS     2
#define AI_SPLIT     3
#define AI_BOSSRUSH  4

/* 색상 속성 */
#define C_DEFAULT (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE)
#define C_WHITE   (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_GREEN   (FOREGROUND_GREEN|FOREGROUND_INTENSITY)
#define C_YELLOW  (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY)
#define C_RED     (FOREGROUND_RED|FOREGROUND_INTENSITY)
#define C_CYAN    (FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_MAGENTA (FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_BLUE    (FOREGROUND_BLUE|FOREGROUND_INTENSITY)
#define C_GRAY    (FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE)

#define BG_HOVER   (BACKGROUND_RED|BACKGROUND_GREEN)
#define BG_SELECT  (BACKGROUND_BLUE|BACKGROUND_INTENSITY)

/* ── 구조체 ── */
typedef struct { int type; int hp; int maxHp; } Block;

typedef struct {
    double x, y, dx, dy;
    int    active;
    double speed;
} Ball;

typedef struct { int type, col, row, active; } Item;

/* ── 저장 구조체 ── */
#define SAVE_MAGIC 0xBD31
typedef struct {
    unsigned short magic;
    int score, wave, shieldActive, slowActive;
    Block map[ROWS][COLS];
} SaveData;

/* ── 더블버퍼 ── */
#define BUF_W 80
#define BUF_H 40
static CHAR_INFO g_buf[BUF_H][BUF_W];

/* ── 전역 변수 ── */
static Block  g_map[ROWS][COLS];
static Ball   g_balls[MAX_BALLS];
static Item   g_items[MAX_ITEMS];

static int g_points, g_wave, g_gameOver, g_score;
static int g_shieldActive, g_slowActive, g_activeBallCount;
static int g_selectedType;
static int g_hoverRow, g_hoverCol;

static HANDLE g_hOut, g_hIn;

/* ===========================================================
   더블버퍼 유틸
=========================================================== */
static void bufClear(void) {
    int r, c;
    for (r = 0; r < BUF_H; r++)
        for (c = 0; c < BUF_W; c++) {
            g_buf[r][c].Char.AsciiChar = ' ';
            g_buf[r][c].Attributes     = (WORD)C_DEFAULT;
        }
}

/* bufPrint: row, col, 문자열, 색상  -- 인자 4개 고정 */
static void bufPrint(int row, int col, const char *s, WORD attr) {
    int i;
    if (row < 0 || row >= BUF_H) return;
    for (i = 0; s[i] != '\0' && (col + i) < BUF_W; i++) {
        g_buf[row][col + i].Char.AsciiChar = s[i];
        g_buf[row][col + i].Attributes     = attr;
    }
}

static void bufFlush(void) {
    SMALL_RECT sr   = {0, 0, BUF_W - 1, BUF_H - 1};
    COORD      size = {BUF_W, BUF_H};
    COORD      orig = {0, 0};
    WriteConsoleOutputA(g_hOut, (CHAR_INFO *)g_buf, size, orig, &sr);
}

/* 정수 → 문자열 (sprintf 없이) */
static void int2str(int v, char *buf) {
    char tmp[12];
    int  i = 0, neg = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (v < 0)  { neg = 1; v = -v; }
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    { int j; for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j]; }
    buf[i] = '\0';
}

/* 정수를 버퍼에 직접 출력 */
static void bufPrintInt(int row, int col, int v, WORD attr) {
    char s[16];
    int2str(v, s);
    bufPrint(row, col, s, attr);
}

/* ===========================================================
   초기화
=========================================================== */
static void initMap(void) {
    int r, c;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++) {
            g_map[r][c].type  = EMPTY;
            g_map[r][c].hp    = 0;
            g_map[r][c].maxHp = 0;
        }
    g_map[ROWS-1][COLS/2].type  = BOSS;
    g_map[ROWS-1][COLS/2].hp    = 5;
    g_map[ROWS-1][COLS/2].maxHp = 5;
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

static int blockCost(int t) { return t==NORMAL?1 : t==HARD?2 : t==MIRROR?3 : 0; }
static int blockHP(int t)   { return t==NORMAL?1 : t==HARD?3 : t==MIRROR?1 : 0; }

/* ===========================================================
   저장 / 로드
=========================================================== */
static int saveGame(void) {
    FILE *f;
    SaveData sd;
    f = fopen(SAVE_FILE, "wb");
    if (!f) return 0;
    sd.magic        = SAVE_MAGIC;
    sd.score        = g_score;
    sd.wave         = g_wave;
    sd.shieldActive = g_shieldActive;
    sd.slowActive   = g_slowActive;
    memcpy(sd.map, g_map, sizeof(g_map));
    fwrite(&sd, sizeof(sd), 1, f);
    fclose(f);
    return 1;
}

static int loadGame(void) {
    FILE *f;
    SaveData sd;
    f = fopen(SAVE_FILE, "rb");
    if (!f) return 0;
    if (fread(&sd, sizeof(sd), 1, f) != 1 || sd.magic != SAVE_MAGIC) {
        fclose(f); return 0;
    }
    g_score        = sd.score;
    g_wave         = sd.wave;
    g_shieldActive = sd.shieldActive;
    g_slowActive   = sd.slowActive;
    memcpy(g_map, sd.map, sizeof(g_map));
    fclose(f);
    return 1;
}

/* ===========================================================
   배치 페이즈 UI 렌더링
=========================================================== */

/* HP 비율에 맞는 색상 반환 */
static WORD hpColor(int hp, int maxHp) {
    double r;
    if (maxHp <= 0) return (WORD)C_GRAY;
    r = (double)hp / (double)maxHp;
    if (r > 0.6) return (WORD)C_GREEN;
    if (r > 0.3) return (WORD)C_YELLOW;
    return (WORD)C_RED;
}

/* 셀 1칸(4자) 문자열과 색상 채우기 */
static void getBlockDisplay(Block b, int isHover, char out[5], WORD *attr) {
    char num[4];
    if (isHover) {
        *attr = (WORD)(C_WHITE | BG_HOVER);
        if      (g_selectedType == NORMAL) { out[0]='['; out[1]='_'; out[2]=']'; out[3]=' '; out[4]='\0'; }
        else if (g_selectedType == HARD)   { out[0]='['; out[1]='='; out[2]=']'; out[3]=' '; out[4]='\0'; }
        else                               { out[0]='['; out[1]='/'; out[2]=']'; out[3]=' '; out[4]='\0'; }
        return;
    }
    *attr = (WORD)C_DEFAULT;
    switch (b.type) {
    case EMPTY:
        out[0]=' '; out[1]='.'; out[2]=' '; out[3]=' '; out[4]='\0';
        break;
    case NORMAL:
        int2str(b.hp, num);
        out[0]='['; out[1]=num[0]; out[2]=']'; out[3]=' '; out[4]='\0';
        *attr = hpColor(b.hp, b.maxHp);
        break;
    case HARD:
        if      (b.hp >= 3) { out[0]='['; out[1]='#'; out[2]=']'; out[3]=' '; out[4]='\0'; }
        else if (b.hp == 2) { out[0]='['; out[1]='+'; out[2]=']'; out[3]=' '; out[4]='\0'; }
        else                { out[0]='['; out[1]='-'; out[2]=']'; out[3]=' '; out[4]='\0'; }
        *attr = hpColor(b.hp, b.maxHp);
        break;
    case MIRROR:
        out[0]='['; out[1]='/'; out[2]=']'; out[3]=' '; out[4]='\0';
        *attr = (WORD)C_CYAN;
        break;
    case BOSS:
        out[0]='['; out[1]='B'; out[2]=']'; out[3]=' '; out[4]='\0';
        *attr = (WORD)(b.hp >= 4 ? (C_MAGENTA|FOREGROUND_INTENSITY) :
                       b.hp >= 2 ? C_MAGENTA : C_RED);
        break;
    default:
        out[0]=' '; out[1]='.'; out[2]=' '; out[3]=' '; out[4]='\0';
        break;
    }
}

/* HP 바 문자열 생성 ("[====      ]") */
static void makeHpBar(int hp, int maxHp, int w, char *out) {
    int i, filled;
    if (maxHp <= 0) { out[0] = '\0'; return; }
    filled = (int)((double)hp / (double)maxHp * (double)w + 0.5);
    out[0] = '[';
    for (i = 0; i < w; i++) out[1 + i] = (i < filled) ? '=' : ' ';
    out[1 + w] = ']';
    out[2 + w] = '\0';
}

/* 구분선 한 줄 */
static void drawSeparator(int row, char ch) {
    char line[BUF_W + 1];
    int  j;
    for (j = 0; j < BUF_W - 1; j++) line[j] = ch;
    line[BUF_W - 1] = '\0';
    bufPrint(row, 0, line, (WORD)C_GRAY);
}

/* 상단 배치 UI 패널 */
static void drawPlacementUI(void) {
    char tmp[64];

    bufPrint(0, 1, "=== BLOCK DEFENSE v3.1  [PLACEMENT] ===", (WORD)C_WHITE);

    /* 포인트 */
    bufPrint(1, 2, "Points: ", (WORD)C_YELLOW);
    bufPrintInt(1, 10, g_points,       (WORD)C_YELLOW);
    bufPrint(1, 12, " / ", (WORD)C_YELLOW);
    bufPrintInt(1, 15, TOTAL_POINTS,   (WORD)C_YELLOW);

    /* 블록 선택 버튼 */
    {
        const char *labels[3] = {"[1] Normal(1pt)", "[2] Hard(2pt) ", "[3] Mirror(3pt)"};
        int types[3]          = {NORMAL, HARD, MIRROR};
        int bx[3]             = {2, 19, 34};
        int i;
        for (i = 0; i < 3; i++) {
            WORD a = (g_selectedType == types[i])
                   ? (WORD)(C_WHITE | BG_SELECT)
                   : (WORD)C_GRAY;
            bufPrint(2, bx[i], labels[i], a);
        }
    }

    /* 조작 버튼 */
    bufPrint(3,  2, "[ ENTER = Done ]",   (WORD)C_GREEN);
    bufPrint(3, 20, "[ S = Save ]",       (WORD)C_CYAN);
    bufPrint(3, 33, "[ L = Load ]",       (WORD)C_CYAN);

    /* 안내 */
    bufPrint(4, 2,
        "LClick:place  RClick:remove  1/2/3:select  Enter:start",
        (WORD)C_GRAY);

    drawSeparator(5, '-');

    /* 열 번호 헤더 */
    {
        int i;
        bufPrint(6, GRID_LEFT - 2, "  ", (WORD)C_GRAY);
        for (i = 0; i < COLS; i++) {
            tmp[0] = (char)('0' + i); tmp[1] = ' '; tmp[2] = ' '; tmp[3] = ' '; tmp[4] = '\0';
            bufPrint(6, GRID_LEFT + i * CELL_W, tmp, (WORD)C_GRAY);
        }
    }
    (void)tmp; /* 미사용 경고 억제 */
}

/* 전투 상태 패널 */
static void drawBattleUI(void) {
    char bar[20];
    int  bossHp = g_map[ROWS-1][COLS/2].hp;

    bufPrint(0, 1, "=== BLOCK DEFENSE v3.1  [BATTLE] ===", (WORD)C_WHITE);

    bufPrint(1, 2, "Wave: ", (WORD)C_YELLOW);
    bufPrintInt(1,  8, g_wave,       (WORD)C_YELLOW);
    bufPrint(1, 10, "/",             (WORD)C_YELLOW);
    bufPrintInt(1, 11, MAX_WAVES,    (WORD)C_YELLOW);

    bufPrint(1, 20, "Score: ",       (WORD)C_YELLOW);
    bufPrintInt(1, 27, g_score,      (WORD)C_YELLOW);

    makeHpBar(bossHp, 5, 10, bar);
    bufPrint(2, 2, "Boss HP: ",      (WORD)C_WHITE);
    bufPrint(2, 11, bar,             hpColor(bossHp, 5));
    bufPrint(2, 24, " ",             (WORD)C_WHITE);
    bufPrintInt(2, 25, bossHp,       hpColor(bossHp, 5));
    bufPrint(2, 26, "/5",            (WORD)C_WHITE);

    if (g_shieldActive > 0) {
        bufPrint(2, 32, "[SHIELD:",   (WORD)C_BLUE);
        bufPrintInt(2, 40, g_shieldActive, (WORD)C_BLUE);
        bufPrint(2, 41, "]",          (WORD)C_BLUE);
    }
    if (g_slowActive > 0) {
        bufPrint(2, 44, "[SLOW:",     (WORD)C_CYAN);
        bufPrintInt(2, 50, g_slowActive, (WORD)C_CYAN);
        bufPrint(2, 51, "]",          (WORD)C_CYAN);
    }

    bufPrint(3, 2, "Balls: ",        (WORD)C_GRAY);
    bufPrintInt(3, 9, g_activeBallCount, (WORD)C_GRAY);

    drawSeparator(5, '-');
}

/* 그리드 전체 그리기 */
static void drawGrid(int phase) {
    int r, c;
    for (r = 0; r < ROWS; r++) {
        char rn[4];
        int2str(r, rn);
        bufPrint(GRID_TOP + r * CELL_H, GRID_LEFT - 2, rn, (WORD)C_GRAY);

        for (c = 0; c < COLS; c++) {
            int   isHover = (phase == 0
                             && r == g_hoverRow && c == g_hoverCol
                             && g_map[r][c].type == EMPTY
                             && r > 0 && r < ROWS - 1);
            char  cell[5];
            WORD  attr;

            getBlockDisplay(g_map[r][c], isHover, cell, &attr);

            /* 아이템 덮어쓰기 (전투 중) */
            if (phase == 1) {
                int ii;
                for (ii = 0; ii < MAX_ITEMS; ii++) {
                    if (!g_items[ii].active) continue;
                    if (g_items[ii].row == r && g_items[ii].col == c) {
                        switch (g_items[ii].type) {
                        case ITEM_SHIELD:    cell[0]='(';cell[1]='S';cell[2]=')';cell[3]=' ';cell[4]='\0'; attr=(WORD)C_BLUE;   break;
                        case ITEM_MULTIBALL: cell[0]='(';cell[1]='M';cell[2]=')';cell[3]=' ';cell[4]='\0'; attr=(WORD)C_YELLOW; break;
                        case ITEM_SLOW:      cell[0]='(';cell[1]='W';cell[2]=')';cell[3]=' ';cell[4]='\0'; attr=(WORD)C_CYAN;   break;
                        case ITEM_REPAIR:    cell[0]='(';cell[1]='R';cell[2]=')';cell[3]=' ';cell[4]='\0'; attr=(WORD)C_GREEN;  break;
                        default: break;
                        }
                        break;
                    }
                }
                /* 볼 덮어쓰기 */
                {
                    int bi;
                    for (bi = 0; bi < MAX_BALLS; bi++) {
                        if (!g_balls[bi].active) continue;
                        if ((int)(g_balls[bi].y + 0.5) == r &&
                            (int)(g_balls[bi].x + 0.5) == c) {
                            cell[0]='(';cell[1]='o';cell[2]=')';cell[3]=' ';cell[4]='\0';
                            attr = (WORD)C_WHITE;
                            break;
                        }
                    }
                }
            }

            bufPrint(GRID_TOP + r * CELL_H, GRID_LEFT + c * CELL_W, cell, attr);
        }
    }
}

/* 하단 범례 */
static void drawLegend(int row) {
    bufPrint(row,  2, "Legend:", (WORD)C_WHITE);
    bufPrint(row, 10, "[1]Nrm",  (WORD)C_GREEN);
    bufPrint(row, 17, "[#]Hard", (WORD)C_YELLOW);
    bufPrint(row, 25, "[/]Mirr", (WORD)C_CYAN);
    bufPrint(row, 33, "[B]Boss", (WORD)C_MAGENTA);
    bufPrint(row, 41, "(S)Shld", (WORD)C_BLUE);
    bufPrint(row, 49, "(M)Mbal", (WORD)C_YELLOW);
    bufPrint(row, 57, "(W)Slow", (WORD)C_CYAN);
    bufPrint(row, 65, "(R)Rpr",  (WORD)C_GREEN);
}

static void renderPlacement(void) {
    bufClear();
    drawPlacementUI();
    drawGrid(0);
    drawLegend(GRID_TOP + ROWS * CELL_H + 2);
    bufFlush();
}

static void renderBattle(void) {
    bufClear();
    drawBattleUI();
    drawGrid(1);
    drawLegend(GRID_TOP + ROWS * CELL_H + 2);
    bufFlush();
}

/* 메시지 행 표시 후 잠깐 대기 */
static void showMsg(const char *msg, WORD attr, int ms) {
    /* 메시지 행을 공백으로 지운 뒤 출력 */
    bufPrint(MSG_ROW, 0,
        "                                                  ",
        (WORD)C_DEFAULT);
    bufPrint(MSG_ROW, 2, msg, attr);
    bufFlush();
    Sleep((DWORD)ms);
}

/* ===========================================================
   마우스 좌표 변환
=========================================================== */
/* 반환 1=그리드 내, 0=바깥 */
static int mouseToGrid(int mx, int my, int *outRow, int *outCol) {
    int c = (mx - GRID_LEFT) / CELL_W;
    int r = (my - GRID_TOP)  / CELL_H;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    *outRow = r; *outCol = c;
    return 1;
}

/* 0=없음, 1~3=블록타입, 10=완료, 11=저장, 12=로드 */
static int mouseToButton(int mx, int my) {
    if (my == 2) {
        if (mx >= 2  && mx <= 16) return 1;
        if (mx >= 19 && mx <= 31) return 2;
        if (mx >= 34 && mx <= 48) return 3;
    }
    if (my == 3) {
        if (mx >= 2  && mx <= 17) return 10;
        if (mx >= 20 && mx <= 31) return 11;
        if (mx >= 33 && mx <= 44) return 12;
    }
    return 0;
}

/* ===========================================================
   배치 페이즈 (마우스 GUI)
=========================================================== */
static void placementPhase(void) {
    INPUT_RECORD ir;
    DWORD        numRead;
    int          done = 0;
    int          gr, gc;

    g_selectedType = NORMAL;
    g_points       = TOTAL_POINTS;
    g_hoverRow     = -1;
    g_hoverCol     = -1;

    SetConsoleMode(g_hIn, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    while (!done) {
        renderPlacement();
        ReadConsoleInput(g_hIn, &ir, 1, &numRead);

        /* ── 마우스 이벤트 ── */
        if (ir.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD *me = &ir.Event.MouseEvent;
            int mx = (int)me->dwMousePosition.X;
            int my = (int)me->dwMousePosition.Y;

            /* 호버 */
            if (me->dwEventFlags == MOUSE_MOVED) {
                if (mouseToGrid(mx, my, &gr, &gc)) { g_hoverRow = gr; g_hoverCol = gc; }
                else                               { g_hoverRow = -1; g_hoverCol = -1; }
            }

            /* 좌클릭 */
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
                int btn = mouseToButton(mx, my);
                if (btn >= 1 && btn <= 3) {
                    g_selectedType = btn;
                }
                else if (btn == 10) {
                    done = 1;
                }
                else if (btn == 11) {
                    if (saveGame()) showMsg(">> Saved!",        (WORD)C_GREEN, 700);
                    else            showMsg(">> Save failed.",  (WORD)C_RED,   700);
                }
                else if (btn == 12) {
                    if (loadGame()) showMsg(">> Loaded!",       (WORD)C_GREEN, 700);
                    else            showMsg(">> No save file.", (WORD)C_RED,   700);
                }
                else if (mouseToGrid(mx, my, &gr, &gc)) {
                    if (gr > 0 && gr < ROWS-1 && g_map[gr][gc].type == EMPTY) {
                        int cost = blockCost(g_selectedType);
                        if (g_points >= cost) {
                            g_map[gr][gc].type  = g_selectedType;
                            g_map[gr][gc].hp    = blockHP(g_selectedType);
                            g_map[gr][gc].maxHp = blockHP(g_selectedType);
                            g_points -= cost;
                        } else {
                            showMsg(">> Not enough points!", (WORD)C_RED, 600);
                        }
                    }
                }
            }

            /* 우클릭 – 블록 제거 & 포인트 환불 */
            if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) {
                if (mouseToGrid(mx, my, &gr, &gc)) {
                    if (gr > 0 && gr < ROWS-1
                        && g_map[gr][gc].type != EMPTY
                        && g_map[gr][gc].type != BOSS) {
                        g_points += blockCost(g_map[gr][gc].type);
                        g_map[gr][gc].type  = EMPTY;
                        g_map[gr][gc].hp    = 0;
                        g_map[gr][gc].maxHp = 0;
                    }
                }
            }
        }

        /* ── 키보드 이벤트 ── */
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            char  k  = ir.Event.KeyEvent.uChar.AsciiChar;
            WORD  vk = ir.Event.KeyEvent.wVirtualKeyCode;

            if      (k == '1') g_selectedType = NORMAL;
            else if (k == '2') g_selectedType = HARD;
            else if (k == '3') g_selectedType = MIRROR;
            else if (k == 's' || k == 'S') {
                if (saveGame()) showMsg(">> Saved!",        (WORD)C_GREEN, 700);
                else            showMsg(">> Save failed.",  (WORD)C_RED,   700);
            }
            else if (k == 'l' || k == 'L') {
                if (loadGame()) showMsg(">> Loaded!",       (WORD)C_GREEN, 700);
                else            showMsg(">> No save file.", (WORD)C_RED,   700);
            }
            else if (vk == VK_RETURN) { done = 1; }
        }
    }
}

/* ===========================================================
   AI 로직
=========================================================== */
static int getColumnScore(int col) {
    int r, s = 0;
    for (r = 0; r < ROWS; r++) s += g_map[r][col].hp;
    return s;
}

static int findWeakestColumn(void) {
    int c, minS = 99999, weakCol = 0;
    for (c = 0; c < COLS; c++) {
        int s = getColumnScore(c);
        if (s < minS) { minS = s; weakCol = c; }
    }
    return weakCol;
}

static int getAIPattern(int w) {
    if (w <= 1) return AI_RANDOM;
    if (w == 2) return AI_SCAN;
    if (w == 3) return AI_FOCUS;
    if (w == 4) return AI_SPLIT;
    return AI_BOSSRUSH;
}

static int getAIBallCount(int p) {
    return (p == AI_FOCUS) ? 2 :
           (p == AI_SPLIT) ? 3 :
           (p == AI_BOSSRUSH) ? 2 : 1;
}

static int getTargetColumn(int pat, int bi) {
    int wk = findWeakestColumn();
    int t;
    switch (pat) {
    case AI_RANDOM:   return rand() % COLS;
    case AI_SCAN:     return (rand() % 3 == 0) ? wk : rand() % COLS;
    case AI_FOCUS:    t = wk + (rand() % 3) - 1; return (t<0)?0:(t>=COLS)?COLS-1:t;
    case AI_SPLIT:    { int arr[3] = {wk, COLS/4, COLS*3/4}; return arr[bi % 3]; }
    case AI_BOSSRUSH: return COLS / 2;
    default:          return rand() % COLS;
    }
}

/* ===========================================================
   아이템
=========================================================== */
static void dropItem(int row, int col) {
    int i;
    if (rand() % 10 >= 3) return;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (!g_items[i].active) {
            g_items[i].active = 1;
            g_items[i].row    = row;
            g_items[i].col    = col;
            g_items[i].type   = (rand() % 4) + 1;
            return;
        }
    }
}

static void applyItem(int idx) {
    int r, c, found;
    switch (g_items[idx].type) {
    case ITEM_SHIELD:
        g_map[ROWS-1][COLS/2].hp += 2;
        if (g_map[ROWS-1][COLS/2].hp > 5) g_map[ROWS-1][COLS/2].hp = 5;
        g_shieldActive = 2;
        break;
    case ITEM_MULTIBALL:
        g_score += 50;
        break;
    case ITEM_SLOW:
        g_slowActive = 1;
        break;
    case ITEM_REPAIR:
        found = 0;
        for (r = 1; r < ROWS-1 && !found; r++)
            for (c = 0; c < COLS && !found; c++)
                if (g_map[r][c].type != EMPTY && g_map[r][c].hp < g_map[r][c].maxHp) {
                    g_map[r][c].hp++;
                    found = 1;
                }
        if (!found) g_score += 30;
        break;
    default: break;
    }
    g_items[idx].active = 0;
}

static void checkItemPickup(int bx, int by) {
    int i;
    for (i = 0; i < MAX_ITEMS; i++)
        if (g_items[i].active && g_items[i].col == bx && g_items[i].row == by)
            applyItem(i);
}

/* ===========================================================
   볼 물리
=========================================================== */
static void hitBlock(int r, int c) {
    int bi;
    if (g_map[r][c].type == EMPTY) return;

    if (g_map[r][c].type == MIRROR) {
        for (bi = 0; bi < MAX_BALLS; bi++)
            if (g_balls[bi].active) g_balls[bi].dx = -g_balls[bi].dx;
        if (--g_map[r][c].hp <= 0) { dropItem(r,c); g_map[r][c].type = EMPTY; }
        return;
    }

    for (bi = 0; bi < MAX_BALLS; bi++)
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
        g_map[r][c].type  = EMPTY;
        g_map[r][c].maxHp = 0;
    }
}

static void moveSingleBall(int bi) {
    Ball *b = &g_balls[bi];
    int   bx, by;

    b->x += b->dx * b->speed;
    b->y += b->dy * b->speed;

    if (b->x < 0)      { b->x = 0;       b->dx = -b->dx; }
    if (b->x >= COLS)  { b->x = COLS-1;  b->dx = -b->dx; }
    if (b->y < 0)      { b->y = 0;        b->dy = -b->dy; }
    if (b->y >= ROWS)  { b->active = 0;  g_activeBallCount--; return; }

    bx = (int)(b->x + 0.5);
    by = (int)(b->y + 0.5);

    if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
        checkItemPickup(bx, by);
        if (g_map[by][bx].type != EMPTY) hitBlock(by, bx);
    }
}

/* ===========================================================
   전투 페이즈
=========================================================== */
static void battlePhase(void) {
    int w, step, bi;
    SetConsoleMode(g_hIn, 0);   /* 전투 중 입력 비활성화 */

    for (w = 1; w <= MAX_WAVES; w++) {
        int pat, cnt, tc;
        double spd;
        char waveMsg[40];

        g_wave = w;
        if (g_gameOver) break;

        pat  = getAIPattern(w);
        cnt  = getAIBallCount(pat);
        spd  = (g_slowActive > 0) ? 0.5 : 1.0;
        if (g_slowActive > 0) g_slowActive--;

        initBalls();
        for (bi = 0; bi < cnt && bi < MAX_BALLS; bi++) {
            tc = getTargetColumn(pat, bi);
            if (tc < 0) tc = 0; if (tc >= COLS) tc = COLS-1;
            g_balls[bi].x      = (double)tc;
            g_balls[bi].y      = 0.0;
            g_balls[bi].dx     = (rand() % 2) ?  0.5 : -0.5;
            g_balls[bi].dy     = 1.0;
            g_balls[bi].active = 1;
            g_balls[bi].speed  = spd;
        }
        g_activeBallCount = cnt;

        /* 웨이브 시작 메시지 */
        waveMsg[0] = '\0';
        switch (pat) {
        case AI_RANDOM:   strcpy(waveMsg, "Wave  : Random attack");        break;
        case AI_SCAN:     strcpy(waveMsg, "Wave  : Scanning weak point..."); break;
        case AI_FOCUS:    strcpy(waveMsg, "Wave  : Focused! (x2 balls)");   break;
        case AI_SPLIT:    strcpy(waveMsg, "Wave  : SPLIT ATTACK! (x3)");    break;
        case AI_BOSSRUSH: strcpy(waveMsg, "Wave  : BOSS RUSH!!! x2");       break;
        default:          strcpy(waveMsg, "Wave  : ...");                   break;
        }
        /* 웨이브 번호 삽입 */
        waveMsg[5] = (char)('0' + w);

        renderBattle();
        showMsg(waveMsg, (WORD)C_WHITE, 1000);

        for (step = 0; step < BALL_STEPS && g_activeBallCount > 0 && !g_gameOver; step++) {
            for (bi = 0; bi < MAX_BALLS; bi++)
                if (g_balls[bi].active) moveSingleBall(bi);
            renderBattle();
            Sleep(100);
        }

        for (bi = 0; bi < MAX_BALLS; bi++) g_balls[bi].active = 0;
        g_activeBallCount = 0;

        if (g_gameOver == -1) break;
        if (g_shieldActive > 0) g_shieldActive--;

        renderBattle();
        Sleep(800);
    }
}

/* ===========================================================
   결과 화면
=========================================================== */
static void showResult(void) {
    INPUT_RECORD ir;
    DWORD        n;

    bufClear();
    bufPrint(2, 4, "+==============================+", (WORD)C_WHITE);
    if (g_gameOver == -1) {
        bufPrint(3, 4, "|  !! GAME OVER               |", (WORD)C_RED);
        bufPrint(4, 4, "|  Boss was destroyed...      |", (WORD)C_RED);
    } else {
        g_score += g_map[ROWS-1][COLS/2].hp * 50;
        bufPrint(3, 4, "|  ** CLEAR! Boss survived! **|", (WORD)C_GREEN);
        bufPrint(4, 4, "|                             |", (WORD)C_WHITE);
    }
    bufPrint(5, 4, "+==============================+", (WORD)C_WHITE);

    bufPrint(6, 4, "|  Final Score  : ",   (WORD)C_YELLOW);
    bufPrintInt(6, 22, g_score,            (WORD)C_YELLOW);
    bufPrint(6, 27, "              |",     (WORD)C_YELLOW);

    bufPrint(7, 4, "|  Wave reached : ",   (WORD)C_WHITE);
    bufPrintInt(7, 22, g_wave,             (WORD)C_WHITE);
    bufPrint(7, 23, " / 5              |", (WORD)C_WHITE);

    bufPrint(8, 4, "|  Boss HP left : ",   (WORD)C_WHITE);
    bufPrintInt(8, 22, g_map[ROWS-1][COLS/2].hp, (WORD)C_WHITE);
    bufPrint(8, 23, " / 5              |", (WORD)C_WHITE);

    bufPrint(9,  4, "+==============================+", (WORD)C_WHITE);
    bufPrint(11, 4, "Press any key to exit...",         (WORD)C_GRAY);
    bufFlush();

    SetConsoleMode(g_hIn, 0);
    do { ReadConsoleInput(g_hIn, &ir, 1, &n); }
    while (!(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown));
}

/* ===========================================================
   메인
=========================================================== */
int main(void) {
    DWORD              outMode = 0;
    CONSOLE_CURSOR_INFO ci;
    SMALL_RECT         wr = {0, 0, BUF_W-1, BUF_H-1};
    COORD              cs = {BUF_W, BUF_H};

    srand((unsigned int)time(NULL));

    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);

    /* ANSI 색상 활성화 */
    GetConsoleMode(g_hOut, &outMode);
    SetConsoleMode(g_hOut,
        outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);

    /* 커서 숨기기 */
    ci.dwSize   = 1;
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &ci);

    /* 콘솔 버퍼/창 크기 조정 */
    SetConsoleScreenBufferSize(g_hOut, cs);
    SetConsoleWindowInfo(g_hOut, TRUE, &wr);

    /* 게임 상태 초기화 */
    initMap();
    initBalls();
    initItems();
    g_gameOver    = 0;
    g_score       = 0;
    g_wave        = 0;
    g_shieldActive = 0;
    g_slowActive   = 0;

    /* 타이틀 화면 */
    bufClear();
    bufPrint(1,  2, "+=========================================+", (WORD)C_WHITE);
    bufPrint(2,  2, "| BLOCK DEFENSE v3.1 - Guard the Boss!   |", (WORD)C_WHITE);
    bufPrint(3,  2, "+=========================================+", (WORD)C_WHITE);
    bufPrint(4,  2, "| NEW v3.1: Mouse-click block placement!  |", (WORD)C_GREEN);
    bufPrint(5,  2, "| LClick=place  RClick=remove  1/2/3=sel  |", (WORD)C_CYAN);
    bufPrint(6,  2, "+=========================================+", (WORD)C_WHITE);
    bufPrint(7,  2, "| Multi-ball / Items / AI 5-level / Save  |", (WORD)C_YELLOW);
    bufPrint(8,  2, "+=========================================+", (WORD)C_WHITE);
    bufPrint(10, 2, "  Press any key to start...",                 (WORD)C_GRAY);
    bufFlush();

    {
        INPUT_RECORD ir; DWORD n;
        SetConsoleMode(g_hIn, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
        do { ReadConsoleInput(g_hIn, &ir, 1, &n); }
        while (!(ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown));
    }

    placementPhase();
    battlePhase();
    showResult();

    /* 커서 복구 */
    ci.dwSize   = 25;
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(g_hOut, &ci);

    return 0;
}
