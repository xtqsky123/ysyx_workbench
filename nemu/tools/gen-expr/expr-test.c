#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <isa.h>
#include <memory/vaddr.h>
#include <utils.h>

void init_regex(void);
word_t expr(char *e, bool *success);

int main(int argc, char *argv[]) {
  int nr_test = 1000;
  if (argc > 1) {
    sscanf(argv[1], "%d", &nr_test);
  }

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "./build/gen-expr %d", nr_test);

  FILE *fp = popen(cmd, "r");
  assert(fp != NULL);

  init_regex();

  int line = 0;
  uint32_t expected = 0;
  char expr_buf[65536];

  while (fscanf(fp, "%u ", &expected) == 1) {
    if (fgets(expr_buf, sizeof(expr_buf), fp) == NULL) {
      break;
    }

    expr_buf[strcspn(expr_buf, "\n")] = '\0';

    bool success = true;
    word_t actual = expr(expr_buf, &success);
    line ++;

    if (!success || actual != (word_t)expected) {
      printf("Mismatch at test %d\n", line);
      printf("expr     = %s\n", expr_buf);
      printf("expected = %u\n", expected);
      printf("actual   = " FMT_WORD "\n", actual);
      pclose(fp);
      return 1;
    }
  }

  pclose(fp);
  printf("expr-test passed %d cases\n", line);
  return 0;
}

CPU_state cpu = {0};
NEMUState nemu_state = {0};
FILE *log_fp = NULL;
unsigned char isa_logo[] = "expr-test";

bool log_enable(void) {
  return false;
}

void assert_fail_msg(void) {
}

void isa_reg_display(void) {
}

word_t isa_reg_str2val(const char *name, bool *success) {
  (void)name;
  *success = false;
  return 0;
}

word_t vaddr_read(vaddr_t addr, int len) {
  fprintf(stderr, "expr-test: unexpected dereference at " FMT_WORD ", len=%d\n", addr, len);
  exit(1);
}
