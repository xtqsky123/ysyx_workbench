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

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>
#include <memory/vaddr.h>

enum {
  TK_NOTYPE = 256, TK_EQ, TK_NEQ, TK_AND, TK_OR,
  TK_DEC, TK_HEX, TK_REG,
  TK_DEREF, TK_NEG, TK_NOT,
  TK_LT, TK_LE, TK_GT, TK_GE,
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  {" +", TK_NOTYPE},    // spaces
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  {"[0-9]+", TK_DEC},
  {"\\$[a-zA-Z0-9_]+", TK_REG},
  {"==", TK_EQ},        // equal
  {"!=", TK_NEQ},
  {"&&", TK_AND},
  {"\\|\\|", TK_OR},
  {"<=", TK_LE},
  {">=", TK_GE},
  {"<", TK_LT},
  {">", TK_GT},
  {"\\+", '+'},         // plus
  {"-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"!", TK_NOT},
  {"\\(", '('},
  {"\\)", ')'},
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[128] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool is_binary_op(int type) {
  return type == '+' || type == '-' || type == '*' || type == '/' ||
    type == TK_EQ || type == TK_NEQ || type == TK_AND || type == TK_OR ||
    type == TK_LT || type == TK_LE || type == TK_GT || type == TK_GE;
}

static int precedence(int type) {
  switch (type) {
    case TK_OR: return 1;
    case TK_AND: return 2;
    case TK_EQ:
    case TK_NEQ: return 3;
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE: return 4;
    case '+':
    case '-': return 5;
    case '*':
    case '/': return 6;
    default: return 0;
  }
}

static word_t read_register(const char *s, bool *success) {
  if (strcmp(s, "$pc") == 0 || strcmp(s, "pc") == 0) {
    return cpu.pc;
  }

  if (s[0] == '$') {
    s ++;
  }

  return isa_reg_str2val(s, success);
}

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          default:
            if (nr_token >= (int)ARRLEN(tokens)) {
              printf("too many tokens\n");
              return false;
            }
            if (substr_len >= (int)sizeof(tokens[nr_token].str)) {
              printf("token too long\n");
              return false;
            }
            tokens[nr_token].type = rules[i].token_type;
            snprintf(tokens[nr_token].str, sizeof(tokens[nr_token].str), "%.*s",
                substr_len, substr_start);
            nr_token ++;
            break;
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  for (i = 0; i < nr_token; i ++) {
    if (tokens[i].type == '-' && (i == 0 || tokens[i - 1].type == '(' || is_binary_op(tokens[i - 1].type))) {
      tokens[i].type = TK_NEG;
    }
    else if (tokens[i].type == '*' && (i == 0 || tokens[i - 1].type == '(' || is_binary_op(tokens[i - 1].type))) {
      tokens[i].type = TK_DEREF;
    }
  }

  return true;
}

static bool check_parentheses(int p, int q) {
  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  int depth = 0;
  for (int i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      depth ++;
    }
    else if (tokens[i].type == ')') {
      depth --;
      if (depth == 0 && i < q) {
        return false;
      }
      if (depth < 0) {
        return false;
      }
    }
  }

  return depth == 0;
}

static int find_dominant_op(int p, int q) {
  int depth = 0;
  int op = -1;
  int min_prio = 100;

  for (int i = p; i <= q; i ++) {
    int type = tokens[i].type;
    if (type == '(') {
      depth ++;
      continue;
    }
    if (type == ')') {
      depth --;
      continue;
    }

    if (depth != 0 || !is_binary_op(type)) {
      continue;
    }

    int prio = precedence(type);
    if (prio <= min_prio) {
      min_prio = prio;
      op = i;
    }
  }

  return op;
}

static word_t eval(int p, int q, bool *success) {
  if (!*success) {
    return 0;
  }

  if (p > q) {
    *success = false;
    return 0;
  }

  if (p == q) {
    switch (tokens[p].type) {
      case TK_DEC:
        return (word_t)strtoull(tokens[p].str, NULL, 10);
      case TK_HEX:
        return (word_t)strtoull(tokens[p].str, NULL, 16);
      case TK_REG:
        return read_register(tokens[p].str, success);
      default:
        *success = false;
        return 0;
    }
  }

  if (check_parentheses(p, q)) {
    return eval(p + 1, q - 1, success);
  }

  if (tokens[p].type == TK_NEG || tokens[p].type == TK_DEREF || tokens[p].type == TK_NOT) {
    word_t val = eval(p + 1, q, success);
    if (!*success) {
      return 0;
    }

    switch (tokens[p].type) {
      case TK_NEG:   return (word_t)(-(sword_t)val);
      case TK_DEREF: return vaddr_read(val, sizeof(word_t));
      case TK_NOT:   return !val;
      default:       break;
    }
  }

  int op = find_dominant_op(p, q);
  if (op < 0) {
    *success = false;
    return 0;
  }

  word_t val1 = eval(p, op - 1, success);
  word_t val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  sword_t sval1 = (sword_t)val1;
  sword_t sval2 = (sword_t)val2;

  switch (tokens[op].type) {
    case '+':  return (word_t)(sval1 + sval2);
    case '-':  return (word_t)(sval1 - sval2);
    case '*':  return (word_t)(sval1 * sval2);
    case '/':  return sval2 == 0 ? (*success = false, 0) : (word_t)(sval1 / sval2);
    case TK_EQ:  return val1 == val2;
    case TK_NEQ: return val1 != val2;
    case TK_AND: return val1 && val2;
    case TK_OR:  return val1 || val2;
    case TK_LT:  return sval1 < sval2;
    case TK_LE:  return sval1 <= sval2;
    case TK_GT:  return sval1 > sval2;
    case TK_GE:  return sval1 >= sval2;
    default:
      *success = false;
      return 0;
  }
}


word_t expr(char *e, bool *success) {
  *success = true;
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  if (nr_token == 0) {
    *success = false;
    return 0;
  }

  return eval(0, nr_token - 1, success);
}
