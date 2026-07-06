#include <cstring>

#include "verilated.h"

#include "sdb.h"
#include "sim.h"

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);

  bool batch = false;
  const char *img_file = nullptr;
  const char *diff_so = nullptr;
  npc::TraceConfig trace_config = {};

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-b") == 0) {
      batch = true;
    } else if (std::strcmp(argv[i], "--itrace") == 0) {
      trace_config.itrace = true;
    } else if (std::strcmp(argv[i], "--mtrace") == 0) {
      trace_config.mtrace = true;
    } else if (std::strcmp(argv[i], "--ftrace") == 0) {
      trace_config.ftrace = true;
    } else if (std::strncmp(argv[i], "--diff=", 7) == 0) {
      diff_so = argv[i] + 7;
    } else if (std::strncmp(argv[i], "--elf=", 6) == 0) {
      trace_config.elf_file = argv[i] + 6;
    } else {
      img_file = argv[i];
    }
  }

  npc::Simulator sim(img_file, diff_so, trace_config);
  if (batch) {
    npc::sdb_set_batch_mode();
  }
  npc::sdb_mainloop(sim);
  return 0;
}
