/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// this should be enough
static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

static size_t buf_len = 0;

static void append_text(const char *s) {
  size_t len = strlen(s);
  assert(buf_len + len < sizeof(buf));
  memcpy(buf + buf_len, s, len + 1);
  buf_len += len;
}

static void gen_rand_expr_with_depth(int depth);

static void gen_num() {
  char num[32];
  unsigned value = (unsigned)(rand() % 1000);
  snprintf(num, sizeof(num), "%u", value);
  append_text(num);
}

static void gen_nonzero_expr_with_depth(int depth) {
  append_text("((");
  gen_rand_expr_with_depth(depth - 1);
  append_text(")+1)");
}

static void gen_rand_expr_with_depth(int depth) {
  if (buf_len > sizeof(buf) - 64 || depth <= 0) {
    gen_num();
    return;
  }

  switch (rand() % 3) {
    case 0:
      gen_num();
      break;
    case 1:
      append_text("(");
      gen_rand_expr_with_depth(depth - 1);
      append_text(")");
      break;
    default:
      append_text("(");
      gen_rand_expr_with_depth(depth - 1);
      char op[2] = {0};
      static const char ops[] = "+-*/";
      op[0] = ops[rand() % (int)(sizeof(ops) - 1)];
      append_text(op);
      if (op[0] == '/') {
        gen_nonzero_expr_with_depth(depth);
      } else {
        gen_rand_expr_with_depth(depth - 1);
      }
      append_text(")");
      break;
  }
}

static void gen_rand_expr() {
  buf[0] = '\0';
  buf_len = 0;
  gen_rand_expr_with_depth(3 + rand() % 3);
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    uint32_t result;
    ret = fscanf(fp, "%u", &result);
    pclose(fp);

    printf("%u %s\n", result, buf);
  }
  return 0;
}
