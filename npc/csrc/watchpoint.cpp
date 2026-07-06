#include "sdb.h"

#include <cstdio>
#include <cstring>

#include "sim.h"

namespace npc {

static constexpr int NR_WP = 32;
static constexpr int WP_EXPR_LEN = 128;

struct Watchpoint {
  int no = 0;
  Watchpoint *next = nullptr;
  char expr[WP_EXPR_LEN] = {};
  word_t old_value = 0;
};

static Watchpoint wp_pool[NR_WP];
static Watchpoint *head = nullptr;
static Watchpoint *free_ = nullptr;
static bool wp_inited = false;

void init_wp_pool() {
  for (int i = 0; i < NR_WP; i++) {
    wp_pool[i].no = i;
    wp_pool[i].next = (i == NR_WP - 1 ? nullptr : &wp_pool[i + 1]);
    wp_pool[i].expr[0] = '\0';
    wp_pool[i].old_value = 0;
  }
  head = nullptr;
  free_ = wp_pool;
  wp_inited = true;
}

static Watchpoint *find_wp(int no, Watchpoint **prev) {
  Watchpoint *p = head;
  Watchpoint *last = nullptr;
  while (p != nullptr) {
    if (p->no == no) {
      if (prev != nullptr) {
        *prev = last;
      }
      return p;
    }
    last = p;
    p = p->next;
  }
  if (prev != nullptr) {
    *prev = nullptr;
  }
  return nullptr;
}

void print_wp() {
  if (!wp_inited) {
    init_wp_pool();
  }
  if (head == nullptr) {
    std::printf("No watchpoints.\n");
    return;
  }

  for (Watchpoint *p = head; p != nullptr; p = p->next) {
    std::printf("%d: %s = 0x%08x\n", p->no, p->expr, p->old_value);
  }
}

int new_wp(char *expr_str) {
  if (!wp_inited) {
    init_wp_pool();
  }
  if (free_ == nullptr) {
    std::printf("No free watchpoint slots.\n");
    return -1;
  }

  bool success = true;
  word_t value = expr(expr_str, &success);
  if (!success) {
    std::printf("Bad expression: %s\n", expr_str);
    return -1;
  }

  Watchpoint *wp = free_;
  free_ = free_->next;
  std::snprintf(wp->expr, sizeof(wp->expr), "%s", expr_str);
  wp->old_value = value;
  wp->next = head;
  head = wp;

  std::printf("Watchpoint %d: %s = 0x%08x\n", wp->no, wp->expr, wp->old_value);
  return wp->no;
}

bool delete_wp(int no) {
  if (!wp_inited) {
    init_wp_pool();
  }
  Watchpoint *prev = nullptr;
  Watchpoint *wp = find_wp(no, &prev);
  if (wp == nullptr) {
    std::printf("Watchpoint %d not found.\n", no);
    return false;
  }

  if (prev == nullptr) {
    head = wp->next;
  } else {
    prev->next = wp->next;
  }

  wp->next = free_;
  free_ = wp;
  return true;
}

bool wp_check() {
  if (!wp_inited) {
    init_wp_pool();
  }
  for (Watchpoint *wp = head; wp != nullptr; wp = wp->next) {
    bool success = true;
    word_t new_value = expr(wp->expr, &success);
    if (!success) {
      std::printf("Failed to evaluate watchpoint %d: %s\n", wp->no, wp->expr);
      return true;
    }

    if (new_value != wp->old_value) {
      std::printf("Watchpoint %d triggered:\n", wp->no);
      std::printf("  %s\n", wp->expr);
      std::printf("  old value = 0x%08x\n", wp->old_value);
      std::printf("  new value = 0x%08x\n", new_value);
      wp->old_value = new_value;
      return true;
    }
  }
  return false;
}

}  // namespace npc
