#pragma once

#include <cstdint>

namespace npc {

class Simulator;

using word_t = uint32_t;
using sword_t = int32_t;

word_t expr(char *e, bool *success);

void sdb_set_batch_mode();
void sdb_mainloop(Simulator &sim);

void init_wp_pool();
void print_wp();
int new_wp(char *expr_str);
bool delete_wp(int no);
bool wp_check();

}  // namespace npc
