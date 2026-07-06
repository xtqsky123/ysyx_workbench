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

#include "sdb.h"

#define NR_WP 32
#define WP_EXPR_LEN 128

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  char expr[WP_EXPR_LEN];
  word_t old_value;

} WP;

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
}

static WP *find_wp(int NO, WP **prev) {
  WP *p = head;
  WP *last = NULL;

  while (p != NULL) {
    if (p->NO == NO) {
      if (prev != NULL) {
        *prev = last;
      }
      return p;
    }
    last = p;
    p = p->next;
  }

  if (prev != NULL) {
    *prev = NULL;
  }
  return NULL;
}

void print_wp() {
  WP *p = head;

  if (p == NULL) {
    printf("No watchpoints.\n");
    return;
  }

  while (p != NULL) {
    printf("%d: %s = " FMT_WORD "\n", p->NO, p->expr, p->old_value);
    p = p->next;
  }
}

int new_wp(char *expr_str) {
  if (free_ == NULL) {
    printf("No free watchpoint slots.\n");
    return -1;
  }

  bool success = true;
  word_t value = expr(expr_str, &success);
  if (!success) {
    printf("Bad expression: %s\n", expr_str);
    return -1;
  }

  WP *wp = free_;
  free_ = free_->next;

  snprintf(wp->expr, sizeof(wp->expr), "%s", expr_str);
  wp->old_value = value;
  wp->next = head;
  head = wp;

  printf("Watchpoint %d: %s = " FMT_WORD "\n", wp->NO, wp->expr, wp->old_value);
  return wp->NO;
}

bool delete_wp(int NO) {
  WP *prev = NULL;
  WP *wp = find_wp(NO, &prev);
  if (wp == NULL) {
    printf("Watchpoint %d not found.\n", NO);
    return false;
  }

  if (prev == NULL) {
    head = wp->next;
  }
  else {
    prev->next = wp->next;
  }

  wp->next = free_;
  free_ = wp;
  return true;
}

bool wp_check() {
  for (WP *wp = head; wp != NULL; wp = wp->next) {
    bool success = true;
    word_t new_value = expr(wp->expr, &success);
    if (!success) {
      printf("Failed to evaluate watchpoint %d: %s\n", wp->NO, wp->expr);
      nemu_state.state = NEMU_ABORT;
      return true;
    }

    if (new_value != wp->old_value) {
      printf("Watchpoint %d triggered:\n", wp->NO);
      printf("  %s\n", wp->expr);
      printf("  old value = " FMT_WORD "\n", wp->old_value);
      printf("  new value = " FMT_WORD "\n", new_value);
      wp->old_value = new_value;
      nemu_state.state = NEMU_STOP;
      return true;
    }
  }

  return false;
}

