#include "sdb.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex.h>

#include "sim.h"

namespace npc {

enum {
  TK_NOTYPE = 256, TK_EQ, TK_NEQ, TK_AND, TK_OR,
  TK_DEC, TK_HEX, TK_REG,
  TK_DEREF, TK_NEG, TK_NOT,
  TK_LT, TK_LE, TK_GT, TK_GE,
};

struct Rule {
  const char *regex;
  int token_type;
};

static Rule rules[] = {
  {" +", TK_NOTYPE},
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  {"[0-9]+", TK_DEC},
  {"\\$[a-zA-Z0-9_]+", TK_REG},
  {"==", TK_EQ},
  {"!=", TK_NEQ},
  {"&&", TK_AND},
  {"\\|\\|", TK_OR},
  {"<=", TK_LE},
  {">=", TK_GE},
  {"<", TK_LT},
  {">", TK_GT},
  {"\\+", '+'},
  {"-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"!", TK_NOT},
  {"\\(", '('},
  {"\\)", ')'},
};

static constexpr int NR_REGEX = sizeof(rules) / sizeof(rules[0]);
static regex_t re[NR_REGEX];
static bool regex_inited = false;

struct Token {
  int type = TK_NOTYPE;
  char str[32] = {};
};

static Token tokens[128];
static int nr_token = 0;
static const char *expr_error = nullptr;

static void init_regex() {
  if (regex_inited) {
    return;
  }

  for (int i = 0; i < NR_REGEX; i++) {
    char error_msg[128];
    int ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, sizeof(error_msg));
      std::fprintf(stderr, "regex compilation failed: %s %s\n", error_msg, rules[i].regex);
      std::exit(1);
    }
  }
  regex_inited = true;
}

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

static bool reg_str2val(const char *s, word_t *val) {
  static const char *names[16] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5"
  };

  CPUState state = get_simulator().cpu_state();
  if (std::strcmp(s, "$pc") == 0 || std::strcmp(s, "pc") == 0) {
    *val = state.pc;
    return true;
  }

  if (s[0] == '$') {
    s++;
  }

  if (std::strcmp(s, "0") == 0) {
    *val = state.gpr[0];
    return true;
  }

  if (s[0] == 'x' && std::isdigit(static_cast<unsigned char>(s[1]))) {
    char *end = nullptr;
    long idx = std::strtol(s + 1, &end, 10);
    if (end != s + 1 && *end == '\0' && idx >= 0 && idx < 16) {
      *val = state.gpr[idx];
      return true;
    }
  }

  for (int i = 0; i < 16; i++) {
    if (std::strcmp(s, names[i]) == 0) {
      *val = state.gpr[i];
      return true;
    }
  }

  if (std::strcmp(s, "fp") == 0) {
    *val = state.gpr[8];
    return true;
  }

  if (std::isdigit(static_cast<unsigned char>(s[0]))) {
    char *end = nullptr;
    long idx = std::strtol(s, &end, 10);
    if (end != s && *end == '\0' && idx >= 0 && idx < 16) {
      *val = state.gpr[idx];
      return true;
    }
  }

  return false;
}

static bool make_token(char *e) {
  int position = 0;
  regmatch_t pmatch;
  nr_token = 0;

  while (e[position] != '\0') {
    int i = 0;
    for (; i < NR_REGEX; i++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;
        position += substr_len;

        if (rules[i].token_type == TK_NOTYPE) {
          break;
        }

        if (nr_token >= static_cast<int>(sizeof(tokens) / sizeof(tokens[0]))) {
          std::printf("too many tokens\n");
          expr_error = "too many tokens";
          return false;
        }
        if (substr_len >= static_cast<int>(sizeof(tokens[nr_token].str))) {
          std::printf("token too long\n");
          expr_error = "token too long";
          return false;
        }

        tokens[nr_token].type = rules[i].token_type;
        std::snprintf(tokens[nr_token].str, sizeof(tokens[nr_token].str), "%.*s", substr_len, substr_start);
        nr_token++;
        break;
      }
    }

    if (i == NR_REGEX) {
      std::printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      expr_error = "invalid token";
      return false;
    }
  }

  for (int i = 0; i < nr_token; i++) {
    if (tokens[i].type == '-' &&
        (i == 0 || tokens[i - 1].type == '(' || is_binary_op(tokens[i - 1].type))) {
      tokens[i].type = TK_NEG;
    } else if (tokens[i].type == '*' &&
        (i == 0 || tokens[i - 1].type == '(' || is_binary_op(tokens[i - 1].type))) {
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
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') {
      depth++;
    } else if (tokens[i].type == ')') {
      depth--;
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

static bool check_syntax() {
  int depth = 0;
  for (int i = 0; i < nr_token; i++) {
    int type = tokens[i].type;
    if (type == '(') {
      depth++;
      if (i + 1 < nr_token && tokens[i + 1].type == ')') {
        expr_error = "empty parentheses";
        return false;
      }
    } else if (type == ')') {
      depth--;
      if (depth < 0) {
        expr_error = "mismatched parentheses";
        return false;
      }
    }
  }

  if (depth != 0) {
    expr_error = "mismatched parentheses";
    return false;
  }

  if (nr_token == 0) {
    expr_error = "empty expression";
    return false;
  }

  if (is_binary_op(tokens[0].type) || is_binary_op(tokens[nr_token - 1].type)) {
    expr_error = "binary operator at invalid position";
    return false;
  }

  for (int i = 0; i + 1 < nr_token; i++) {
    int cur = tokens[i].type;
    int nxt = tokens[i + 1].type;
    if (is_binary_op(cur) && is_binary_op(nxt)) {
      expr_error = "adjacent binary operators";
      return false;
    }
    if ((cur == TK_NEG || cur == TK_DEREF || cur == TK_NOT) && is_binary_op(nxt)) {
      expr_error = "unary operator missing operand";
      return false;
    }
    if ((cur == TK_DEC || cur == TK_HEX || cur == TK_REG || cur == ')') &&
        (nxt == TK_DEC || nxt == TK_HEX || nxt == TK_REG || nxt == '(' ||
         nxt == TK_NEG || nxt == TK_DEREF || nxt == TK_NOT)) {
      expr_error = "missing operator";
      return false;
    }
  }

  return true;
}

static int find_dominant_op(int p, int q) {
  int depth = 0;
  int op = -1;
  int min_prio = 100;

  for (int i = p; i <= q; i++) {
    int type = tokens[i].type;
    if (type == '(') {
      depth++;
      continue;
    }
    if (type == ')') {
      depth--;
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
        return static_cast<word_t>(std::strtoul(tokens[p].str, nullptr, 10));
      case TK_HEX:
        return static_cast<word_t>(std::strtoul(tokens[p].str, nullptr, 16));
      case TK_REG: {
        word_t val = 0;
        if (!reg_str2val(tokens[p].str, &val)) {
          expr_error = "invalid register name";
          *success = false;
          return 0;
        }
        return val;
      }
      default:
        *success = false;
        expr_error = "invalid single token";
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
      case TK_NEG: return static_cast<word_t>(-static_cast<sword_t>(val));
      case TK_DEREF:
        if ((val & 0x3u) != 0) {
          expr_error = "unaligned dereference";
          *success = false;
          return 0;
        }
        return get_simulator().pmem_read(val);
      case TK_NOT: return !val;
      default: break;
    }
  }

  int op = find_dominant_op(p, q);
  if (op < 0) {
    expr_error = "no dominant operator";
    *success = false;
    return 0;
  }

  word_t val1 = eval(p, op - 1, success);
  word_t val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  sword_t sval1 = static_cast<sword_t>(val1);
  sword_t sval2 = static_cast<sword_t>(val2);

  switch (tokens[op].type) {
    case '+': return static_cast<word_t>(sval1 + sval2);
    case '-': return static_cast<word_t>(sval1 - sval2);
    case '*': return static_cast<word_t>(sval1 * sval2);
    case '/':
      if (sval2 == 0) {
        expr_error = "division by zero";
        *success = false;
        return 0;
      }
      return static_cast<word_t>(sval1 / sval2);
    case TK_EQ: return val1 == val2;
    case TK_NEQ: return val1 != val2;
    case TK_AND: return val1 && val2;
    case TK_OR: return val1 || val2;
    case TK_LT: return sval1 < sval2;
    case TK_LE: return sval1 <= sval2;
    case TK_GT: return sval1 > sval2;
    case TK_GE: return sval1 >= sval2;
    default:
      expr_error = "unsupported operator";
      *success = false;
      return 0;
  }
}

word_t expr(char *e, bool *success) {
  init_regex();
  expr_error = nullptr;
  *success = true;
  if (!make_token(e)) {
    *success = false;
    return 0;
  }
  if (!check_syntax()) {
    *success = false;
    return 0;
  }
  return eval(0, nr_token - 1, success);
}

}  // namespace npc
