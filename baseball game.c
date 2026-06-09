#pragma execution_character_set("utf-8")
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <conio.h>

#define DIGITS      3
#define STRIKE_MAX  3
#define BASE_COUNT  3

/* ===================== 플레이어 데이터 ===================== */

/* 플레이어 1 */
char  p1_name[20];
int   p1_answer[DIGITS];
int   p1_bases[BASE_COUNT];
int   p1_score;
int   p1_balls, p1_strikes, p1_outs;
int   p1_skip_next;

/* 플레이어 2 */
char  p2_name[20];
int   p2_answer[DIGITS];
int   p2_bases[BASE_COUNT];
int   p2_score;
int   p2_balls, p2_strikes, p2_outs;
int   p2_skip_next;

/* ===================== 유틸 함수 ===================== */

void clear_input_buffer(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
}

int is_valid_digits(int* arr) {
    int i, j;
    for (i = 0; i < DIGITS; i++)
        if (arr[i] < 0 || arr[i] > 9) return 0;
    for (i = 0; i < DIGITS; i++)
        for (j = i + 1; j < DIGITS; j++)
            if (arr[i] == arr[j]) return 0;
    return 1;
}

int parse_digits_from_line(char* line, int* digits) {
    int i, count = 0;
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] >= '0' && line[i] <= '9') {
            if (count >= DIGITS) return 0;
            digits[count++] = line[i] - '0';
        }
        else if (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
            continue;
        }
        else {
            return 0;
        }
    }
    return count == DIGITS;
}

/* ===================== 규칙 출력 ===================== */
void print_rules(void) {
    printf("========== 야구 숫자 맞추기 변형 게임 규칙 ==========\n\n");
    printf("1. 각 플레이어는 중복 없는 0~9 숫자 3개를 비밀 숫자로 정합니다.\n");
    printf("2. 자기 차례에는 상대의 비밀 숫자 3개를 추측합니다.\n");
    printf("3. 숫자와 위치가 모두 맞으면 스트라이크(S)입니다.\n");
    printf("4. 숫자는 맞지만 위치가 다르면 볼(B)입니다.\n");
    printf("5. 하나도 맞지 않으면 아웃(OUT)입니다.\n\n");
    printf("6. 누적 1B  → 내 주자 한 베이스 진루 (3루 주자 있으면 홈인 +1점)\n");
    printf("7. 누적 3S  → 상대 다음 턴 스킵 (내가 연속 공격)\n");
    printf("8. 누적 1OUT→ 상대 베이스 주자 하나 선택 아웃\n");
    printf("9. 홈런     → 숫자 3개 완전히 맞힘, 베이스 주자 전원 홈인(+3점)\n");
    printf("10. 승리    → 홈런 시 게임 종료, 점수 높은 사람 승리\n");
    printf("            (동점이면 먼저 홈런 친 사람 승리)\n\n");
    system("pause");
    system("cls");
}

/* ===================== 입력 함수 ===================== */

/* player: 1이면 p1, 2이면 p2 */
void get_secret_answer(int player) {
    int i;
    char* name = (player == 1) ? p1_name : p2_name;
    int* answer = (player == 1) ? p1_answer : p2_answer;

    while (1) {
        printf("%s의 비밀 숫자 3개 입력: ", name);
        for (i = 0; i < DIGITS; i++) {
            int ch;
            do { ch = _getch(); } while (ch < '0' || ch > '9');
            answer[i] = ch - '0';
            printf("*");
        }
        printf("\n");
        if (is_valid_digits(answer)) {
            printf("입력 완료. 다음 플레이어에게 넘겨주세요.\n");
            system("pause");
            system("cls");
            return;
        }
        printf("중복 없는 0~9 숫자 3개를 입력해야 합니다.\n");
    }
}

void get_guess(int* guess, const char* name) {
    char line[100];
    printf("\n중복 없는 0~9 숫자 3개를 입력하세요.\n");
    while (1) {
        printf("[%s] 숫자 3개 입력: ", name);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("입력이 잘못되었습니다.\n");
            continue;
        }
        if (!parse_digits_from_line(line, guess)) {
            printf("숫자 3개만 입력하세요. 예: 973 또는 9 7 3\n");
            continue;
        }
        if (!is_valid_digits(guess)) {
            printf("중복 없는 0~9 숫자 3개를 입력하세요.\n");
            continue;
        }
        break;
    }
}

/* ===================== 판정 함수 ===================== */
void judge(int* guess, int* answer, int* strikes, int* balls) {
    int i, j;
    *strikes = 0;
    *balls = 0;
    for (i = 0; i < DIGITS; i++) {
        if (guess[i] == answer[i]) {
            (*strikes)++;
        }
        else {
            for (j = 0; j < DIGITS; j++) {
                if (guess[i] == answer[j]) {
                    (*balls)++;
                    break;
                }
            }
        }
    }
}

/* ===================== 출력 함수 ===================== */

/* player: 1이면 p1, 2이면 p2 */
void print_bases(int player) {
    int* bases = (player == 1) ? p1_bases : p2_bases;
    printf("  베이스: [1루:%s] [2루:%s] [3루:%s]\n",
        bases[0] ? "O" : "-",
        bases[1] ? "O" : "-",
        bases[2] ? "O" : "-");
}

void print_board(void) {
    printf("\n+========================================+\n");
    printf("|           현재 상황                    |\n");
    printf("+========================================+\n");
    printf("| %-10s 점수:%-2d B:%-2d S:%-2d OUT:%-2d\n",
        p1_name, p1_score, p1_balls, p1_strikes, p1_outs);
    print_bases(1);
    printf("+----------------------------------------+\n");
    printf("| %-10s 점수:%-2d B:%-2d S:%-2d OUT:%-2d\n",
        p2_name, p2_score, p2_balls, p2_strikes, p2_outs);
    print_bases(2);
    printf("+========================================+\n\n");
}

/* ===================== 게임 로직 함수 ===================== */

/* player: 1이면 p1 진루, 2이면 p2 진루 */
void advance_bases(int player) {
    int  i;
    int* bases = (player == 1) ? p1_bases : p2_bases;
    int* score = (player == 1) ? &p1_score : &p2_score;
    char* name = (player == 1) ? p1_name : p2_name;

    if (bases[2]) {
        (*score)++;
        printf("  %s의 3루 주자 홈인! +1점\n", name);
    }
    for (i = 2; i > 0; i--)
        bases[i] = bases[i - 1];
    bases[0] = 1;
    printf("  %s의 주자가 진루했습니다.\n", name);
}

/* target_player: 주자를 제거당하는 쪽 (1 또는 2) */
void choose_remove_runner(int target_player) {
    int  base;
    int* bases = (target_player == 1) ? p1_bases : p2_bases;
    char* name = (target_player == 1) ? p1_name : p2_name;

    if (!bases[0] && !bases[1] && !bases[2]) {
        printf("  제거할 상대 주자가 없습니다.\n");
        return;
    }
    while (1) {
        printf("  제거할 %s의 주자를 선택하세요 (1:1루, 2:2루, 3:3루): ", name);
        if (scanf("%d", &base) != 1) {
            clear_input_buffer();
            printf("  잘못된 입력입니다.\n");
            continue;
        }
        clear_input_buffer();
        if (base < 1 || base > 3) {
            printf("  1, 2, 3 중에서 선택하세요.\n");
            continue;
        }
        if (!bases[base - 1]) {
            printf("  해당 베이스에는 주자가 없습니다.\n");
            continue;
        }
        bases[base - 1] = 0;
        printf("  %s의 %d루 주자를 아웃시켰습니다.\n", name, base);
        break;
    }
}

/* attacker_player: 홈런 친 쪽 (1 또는 2) */
void apply_homerun(int attacker_player) {
    int  i;
    int* bases = (attacker_player == 1) ? p1_bases : p2_bases;
    int* score = (attacker_player == 1) ? &p1_score : &p2_score;
    char* name = (attacker_player == 1) ? p1_name : p2_name;

    printf("\n  ★ 홈런! %s가 숫자 3개를 모두 맞췄습니다! (기본 3점 획득) ★\n", name);

    // 베이스에 있던 주자들은 홈런 타구에 모두 홈으로 들어오며 베이스를 비웁니다.
    for (i = 0; i < BASE_COUNT; i++) {
        bases[i] = 0;
    }

    (*score) += 3; // 묻지도 따지지도 않고 3점 추가!
    printf("  %s의 점수가 3점 추가되었습니다.\n", name);
}

/* ===================== 메인 함수 ===================== */
int main(void) {
    int turn = 0;   /* 0: p1 공격, 1: p2 공격 */
    int first_solver = 0;
    int game_over = 0;
    int guess[DIGITS];
    int s, b;

    /* 현재 공격자/수비자를 나타내는 정수 (플레이어 1 또는 2) */
    int atk, def;

    /* 공격자의 각 카운터 포인터 */
    char* atk_name;
    int* atk_balls;
    int* atk_strikes;
    int* atk_outs;
    int* def_skip;
    int* def_answer;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /* 전역 변수 초기화 */
    memset(p1_name, 0, sizeof(p1_name));
    memset(p2_name, 0, sizeof(p2_name));
    memset(p1_answer, 0, sizeof(p1_answer));
    memset(p2_answer, 0, sizeof(p2_answer));
    memset(p1_bases, 0, sizeof(p1_bases));
    memset(p2_bases, 0, sizeof(p2_bases));
    p1_score = p2_score = 0;
    p1_balls = p2_balls = 0;
    p1_strikes = p2_strikes = 0;
    p1_outs = p2_outs = 0;
    p1_skip_next = p2_skip_next = 0;

    /* 이름 입력 */
    printf("=== 야구 숫자 맞추기 변형 게임 ===\n\n");
    printf("플레이어 1 이름: ");
    scanf("%10s", p1_name);
    printf("플레이어 2 이름: ");
    scanf("%10s", p2_name);
    clear_input_buffer();
    system("cls");
    print_rules();


    printf("[숫자 설정]\n");
    get_secret_answer(1);
    printf("[숫자 설정]\n");
    get_secret_answer(2);

    printf("게임 시작!\n\n");

    /* ===================== 게임 루프 ===================== */
    while (!game_over) {
        atk = (turn == 0) ? 1 : 2;
        def = (turn == 0) ? 2 : 1;


        atk_name = (atk == 1) ? p1_name : p2_name;
        atk_balls = (atk == 1) ? &p1_balls : &p2_balls;
        atk_strikes = (atk == 1) ? &p1_strikes : &p2_strikes;
        atk_outs = (atk == 1) ? &p1_outs : &p2_outs;
        def_skip = (def == 1) ? &p1_skip_next : &p2_skip_next;
        def_answer = (def == 1) ? p1_answer : p2_answer;


        if ((atk == 1 && p1_skip_next) || (atk == 2 && p2_skip_next)) {
            printf("\n>>> %s의 턴이 스킵됩니다.\n", atk_name);
            if (atk == 1) p1_skip_next = 0;
            else          p2_skip_next = 0;
            turn = 1 - turn;
            continue;
        }

        print_board();
        printf(">>> %s 차례\n", atk_name);
        get_guess(guess, atk_name);
        judge(guess, def_answer, &s, &b);

        printf("  결과: %dS %dB", s, b);
        if (s == 0 && b == 0) printf(" (OUT)");
        printf("\n");

        /* ==================== 수정된 판정 처리 구간 ==================== */

                /* 홈런 판정 (3S 적중) */
        if (s == DIGITS) {
            if (first_solver == 0)
                first_solver = atk;
            apply_homerun(atk);
            game_over = 1;
            break;
        }

        /* 볼 처리: 이제 누적 안 하고 1B당 'b번' 즉시 진루합니다 */
        if (b > 0) {
            printf("  %s의 %dB 판정! 주자가 %d칸 진루합니다.\n", atk_name, b, b);

            for (int k = 0; k < b; k++) {
                advance_bases(atk);
            }
        }

        /* 스트라이크 처리: 스킵 룰은 기존 유지 (3S 누적 시 상대 다음 턴 스킵) */
        if (s > 0) {
            *atk_strikes += s;

            printf("  %s 누적 S: %d/%d\n", atk_name, *atk_strikes, STRIKE_MAX);
            while (*atk_strikes >= STRIKE_MAX) {
                *atk_strikes -= STRIKE_MAX;
                *def_skip = 1;
                printf("  %s의 다음 턴이 스킵됩니다. %s가 한 번 더 공격!\n",
                    (def == 1) ? p1_name : p2_name, atk_name);
            }
        }

        /* 아웃 처리: 이제 1번만 아웃당해도(s==0 && b==0) 즉시 상대 주자 1명 저격 */
        if (s == 0 && b == 0) {
            printf("  %s의 OUT 판정! 상대 주자를 하나 제거할 기회입니다.\n", atk_name);
            choose_remove_runner(def);
        }

        /* ================================================================ */

        turn = 1 - turn;
    }


    /* ===================== 최종 결과 ===================== */
    print_board();
    printf("\n========== 최종 결과 ==========\n");
    printf("%s: %d점\n", p1_name, p1_score);
    printf("%s: %d점\n", p2_name, p2_score);

    if (p1_score > p2_score) {
        printf("[우승] %s!\n", p1_name);
    }
    else if (p2_score > p1_score) {
        printf("[우승] %s!\n", p2_name);
    }
    else {
        printf("동점! 먼저 숫자를 맞춘 사람이 승리합니다.\n");
        if (first_solver == 1)
            printf("[우승] %s!\n", p1_name);
        else if (first_solver == 2)
            printf("[우승] %s!\n", p2_name);
    }

    return 0;
}