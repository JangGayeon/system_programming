#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define STRICMP _stricmp
#else
  #include <strings.h>
  #define STRICMP strcasecmp
#endif

#define MAX_LINES 5000
#define MAX_OPERAND 3 // 한 명령어 당 최대 3개의 operand
#define MAX_INST 256

// ---------------- 구조체 정의 ----------------
struct token_unit {
    char *label;                       // 명령어 라인 중 label(symbol(예 : CLOOP, ENDFIL))
    char *operator;                    // 명령어 라인 중 operator(LDA, JSUB 등0)
    char operand[MAX_OPERAND][50];     // 명령어 라인 중 operand(피연산자 목록(LENGTH, ZERO 등))
    char comment[200];                 // 명령어 라인 중 comment(주석)
};
typedef struct token_unit token;  // 파싱된 토큰 정보를 라인별로 관리하는 테이블
token *token_table[MAX_LINES];  // 원본 소스 코드를 라인별로 저장하는 테이블
char *input_data[MAX_LINES]; //현재까지 읽기/파싱한 라인의 수
static int line_num = 0;

// 명령어 테이블 구조체
struct inst_unit {
    char str[16];          // instruction의 이름
    unsigned char op;      // 명령어의 OPCODE(16진수)
    int format;            // instruction의 형식(FORMAT 1,2,3)
    int ops;               // operand 개수
};
typedef struct inst_unit inst;
inst *inst_table[MAX_INST];  // 명령어 정보를 관리하는 테이블
int inst_index = 0; // 현재 로드된 명령어의 개수

// ---------------- 함수 선언 ----------------
void load_inst_table(const char *filename);  // inst.data.txt 파일 로드
void parse_input(const char *filename);  // input.asm 파일 파싱
unsigned char find_opcode(const char *mnemonic); // 주어진 명령어 이름으로 OPCODE 찾기
int is_directive(const char *mnemonic);  // 주어진 이름이 어셈블러 지시문인지 판별
void print_result(void);  // 파싱 결과 출력하는 함수
void free_all(void);  // 프로그램 종료 시 동적 할당된 메모리 해제

// ---------------- main ----------------
int main(void) {
    load_inst_table("inst.data.txt"); // inst.data.txt 파일 로드->명령어 테이블 =생성
    parse_input("input.asm"); // input.asm 파일 파싱->토큰 테이블 생성
    print_result();
    free_all();
    return 0;
}

// ---------------- 구현부 ----------------

// inst.data.txt 로드
void load_inst_table(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: %s 파일을 열 수 없습니다.\n", filename);
        exit(1);
    }

    char name[16], type[8], opcode_str[16];
    int format;
    inst_index = 0;
    
    //명령어 테이블 로드(이름, 타입, 포맷, OPCODE(16진수 문자열))
    while (fscanf(fp, "%15s %7s %d %15s", name, type, &format, opcode_str) == 4) {
        inst *new_inst = (inst *)malloc(sizeof(inst));
        if (!new_inst) { fprintf(stderr, "malloc failed\n"); exit(1); }

        // 명령어 이름 저장
        strncpy(new_inst->str, name, sizeof(new_inst->str)-1);
        new_inst->str[sizeof(new_inst->str)-1] = '\0';
        new_inst->op = (unsigned char)strtol(opcode_str, NULL, 16); 
        
        // 문자열 형태의 16진수를 숫자로 변환해서 저장
        new_inst->format = format;
        new_inst->ops = 1;
        inst_table[inst_index++] = new_inst;
        if (inst_index >= MAX_INST) break;
    }
    fclose(fp);
    printf("[INFO] 명령어 테이블 %d개 로드 완료.\n", inst_index);
}

// opcode 찾기 (대소문자 무시)
// 찾고자 하는 명령어 이름
// 해당 명령어의 OPECODE 반환, 없으면 0xFF 반환
unsigned char find_opcode(const char *mnemonic) {
    if (!mnemonic) return 0xFF;
    // inst_table 순회하며 mnemonic과 일치하는지 확인
    for (int i = 0; i < inst_index; i++) {
        if (STRICMP(inst_table[i]->str, mnemonic) == 0)
            return inst_table[i]->op;
    }
    return 0xFF;
}

// pseudo directive 판별
// 주어진 이름이 SIC 어셈블러 지시문인지 판별(지시문이면 1, 아니면 0 반환)
int is_directive(const char *mnemonic) {
    if (!mnemonic) return 0;
    const char *dirs[] = {"START","END","WORD","RESW","RESB","BYTE",
                          "EQU","LTORG","CSECT","EXTDEF","EXTREF","BASE", NULL};
    for (int i = 0; dirs[i] != NULL; i++) {
        if (STRICMP(dirs[i], mnemonic) == 0) return 1;
    }
    return 0;
}

// input.asm 토큰 단위로 파싱
void parse_input(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: %s 파일을 열 수 없습니다.\n", filename);
        exit(1);
    }

    char linebuf[1024];
    line_num = 0;

    while (fgets(linebuf, sizeof(linebuf), fp)) {
        // 1. 개행문자 제거(\n, \r\n)
        linebuf[strcspn(linebuf, "\r\n")] = 0;

        // 2. 라인의 시작 부분에서 공백/탭을 건너뛰고 주석이나 빈 줄인지 확인
        char *p = linebuf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '.' || *p == '\0') continue;

        // 3. 토큰화 및 구조체에 저장
        token *tk = (token *)malloc(sizeof(token));
        if (!tk) { fprintf(stderr, "malloc failed\n"); exit(1); }
        memset(tk, 0, sizeof(token));

        // 4. strtok 사용 위해 라인 복사(in 버퍼)
        char temp[1024];
        strncpy(temp, linebuf, sizeof(temp)-1);
        temp[sizeof(temp)-1] = '\0';

        // 5. 라인을 공백/탭을 구분자로 사용하여 토큰화(최대 4개)
        char *t1 = NULL, *t2 = NULL, *t3 = NULL, *t4 = NULL;
        t1 = strtok(temp, " \t");
        if (t1) t2 = strtok(NULL, " \t");
        if (t2) t3 = strtok(NULL, " \t");
        // 남은 부분은 주석일 수 있으므로 개행문자까지 포함
        if (t3) t4 = strtok(NULL, "\n");

        // 6. 토큰의 의미(Label, Operator, Operand) 판별 및 저장
        // case 1: Label이 없는 경우(첫 번째 토큰이 명령어(operation) 또는 지시문(directive))
        if (t1 && (find_opcode(t1) != 0xFF || is_directive(t1))) {
            tk->label = NULL;
            tk->operator = strdup(t1);
            if (t2) strncpy(tk->operand[0], t2, sizeof(tk->operand[0])-1);
            if (t3) strncpy(tk->operand[1], t3, sizeof(tk->operand[1])-1);
            if (t4) {
                while (*t4 == ' ' || *t4 == '\t') t4++;
                strncpy(tk->operand[2], t4, sizeof(tk->operand[2])-1);
            }
        } 
        // case 2 : Label이 있는 경우(첫 번째 토큰이 심볼(label))
        else {
            tk->label = t1 ? strdup(t1) : NULL;
            tk->operator = t2 ? strdup(t2) : NULL;
            if (t3) strncpy(tk->operand[0], t3, sizeof(tk->operand[0])-1);
            if (t4) {
                while (*t4 == ' ' || *t4 == '\t') t4++;
                strncpy(tk->operand[1], t4, sizeof(tk->operand[1])-1);
            }
        }
        
        // 7. 원본 라인 및 파싱 결과 저장
        input_data[line_num] = strdup(linebuf);
        token_table[line_num] = tk;
        line_num++;
        if (line_num >= MAX_LINES) break;
    }

    fclose(fp);
    printf("[INFO] 입력 파일 %d라인 파싱 완료.\n", line_num);
}

// 결과 출력
// OPCODE가 있는 명령어와 지시문을 구분하여 보여줌
void print_result(void) {
    printf("\n[OUTPUT RESULT]\n");
    printf("=====================================================\n");
    printf("%-8s %-8s %-22s %-16s\n", "LABEL", "OP", "OPERANDS", "NOTE");
    printf("-----------------------------------------------------\n");

    for (int i = 0; i < line_num; i++) {
        token *tk = token_table[i];
        if (!tk) continue;
        const char *lbl = tk->label ? tk->label : "";
        const char *op = tk->operator ? tk->operator : "";


        // 모든 operand들을 하나의 문자열로 결합(,로 구분)
        char ops_combined[200] = "";
        int printed = 0;
        for (int k = 0; k < MAX_OPERAND; k++) {
            if (tk->operand[k][0]) {
                if (printed) strncat(ops_combined, ", ", sizeof(ops_combined)-strlen(ops_combined)-1);
                strncat(ops_combined, tk->operand[k], sizeof(ops_combined)-strlen(ops_combined)-1);
                printed = 1;
            }
        }

        // operator가 없는 경우(Label만 있는 경우)
        if (op[0] == '\0') {
            printf("%-8s %-8s %-22s %-16s\n", lbl, "", "", "");
            continue;
        }

        // OPCODE 찾기
        unsigned char opcode = find_opcode(op);
        // OPCODE가 있는 명령어인 경우(일반적인 SIC/XE 명령어)
        if (opcode != 0xFF) {
            printf("%-8s %-8s %-22s OPCODE: %02X\n", lbl, op, ops_combined, opcode);
        } 
        // OPCODE가 없지만 지시문 목록에 존재(START, END 등)
        else if (is_directive(op)) {
            printf("%-8s %-8s %-22s %-16s\n", lbl, op, ops_combined, "DIRECTIVE");
        }
        // 알 수 없는 명령어인 경우(오타/지원하지 않는 명령어)
        else {
            printf("%-8s %-8s %-22s %-16s\n", lbl, op, ops_combined, "Unknown Instruction");
        }
    }
    printf("=====================================================\n");
}

// 메모리 해제
void free_all(void) {
    for (int i = 0; i < line_num; i++) {
        if (token_table[i]) {
            if (token_table[i]->label) free(token_table[i]->label);
            if (token_table[i]->operator) free(token_table[i]->operator);
            free(token_table[i]);
            token_table[i] = NULL;
        }
        if (input_data[i]) {
            free(input_data[i]);
            input_data[i] = NULL;
        }
    }
    for (int i = 0; i < inst_index; i++) {
        if (inst_table[i]) {
            free(inst_table[i]);
            inst_table[i] = NULL;
        }
    }
}
