#include "sdb.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "sim.h"

namespace npc {

static int is_batch_mode = false;
static Simulator *current_sim = nullptr;

static std::string rl_gets() {
  std::string line;
  std::cout << "(npc) " << std::flush;
  std::getline(std::cin, line);
  return line;
}

static int cmd_c(char *args) {
  (void)args;
  current_sim->run(~0ul);
  return 0;
}

static int cmd_si(char *args) {
  unsigned long n = 1;
  if (args != nullptr) {
    while (*args == ' ') {
      args++;
    }
    if (*args != '\0') {
      n = std::strtoul(args, nullptr, 10);
      if (n == 0) {
        n = 1;
      }
    }
  }

  current_sim->clear_stop();
  while (n-- > 0) {
    current_sim->step(true);
    if (current_sim->halted() || current_sim->stop_requested()) {
      break;
    }
  }
  current_sim->print_state();
  current_sim->print_trap();
  return 0;
}

static int cmd_info(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: info r|w|trace\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  if (std::strcmp(args, "r") == 0) {
    current_sim->dump_registers();
  } else if (std::strcmp(args, "w") == 0) {
    print_wp();
  } else if (std::strcmp(args, "trace") == 0) {
    current_sim->print_trace_status();
  } else {
    std::cout << "Unknown info subcommand '" << args << "'\n";
  }
  return 0;
}

static int cmd_x(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: x N EXPR\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  char *end = nullptr;
  long n = std::strtol(args, &end, 10);
  if (end == args) {
    std::cout << "Usage: x N EXPR\n";
    return 0;
  }

  while (*end == ' ') {
    end++;
  }

  bool success = true;
  word_t addr = expr(end, &success);
  if (!success) {
    std::cout << "Bad expression: " << end << "\n";
    return 0;
  }

  for (long i = 0; i < n; i++) {
    word_t cur = addr + static_cast<word_t>(i * 4);
    word_t data = current_sim->pmem_read(cur);
    std::printf("0x%08x: 0x%08x\n", cur, data);
  }
  return 0;
}

static int cmd_p(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: p EXPR\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  bool success = true;
  word_t value = expr(args, &success);
  if (!success) {
    std::cout << "Bad expression: " << args << "\n";
    return 0;
  }

  std::printf("0x%08x (%u)\n", value, value);
  return 0;
}

static int cmd_w(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: w EXPR\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  if (new_wp(args) < 0) {
    std::cout << "Failed to create watchpoint.\n";
  }
  return 0;
}

static int cmd_d(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: d N\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  int no = static_cast<int>(std::strtol(args, nullptr, 10));
  if (!delete_wp(no)) {
    std::cout << "Failed to delete watchpoint " << no << "\n";
  }
  return 0;
}

static int cmd_trace(char *args) {
  if (args == nullptr) {
    std::cout << "Usage: trace {i|m|f} {on|off}\n";
    return 0;
  }

  while (*args == ' ') {
    args++;
  }

  char *type = std::strtok(args, " ");
  char *state = std::strtok(nullptr, " ");
  if (type == nullptr || state == nullptr) {
    std::cout << "Usage: trace {i|m|f} {on|off}\n";
    return 0;
  }

  bool enabled = false;
  if (std::strcmp(state, "on") == 0) {
    enabled = true;
  } else if (std::strcmp(state, "off") != 0) {
    std::cout << "Usage: trace {i|m|f} {on|off}\n";
    return 0;
  }

  if (std::strcmp(type, "i") == 0) {
    current_sim->set_itrace(enabled);
  } else if (std::strcmp(type, "m") == 0) {
    current_sim->set_mtrace(enabled);
  } else if (std::strcmp(type, "f") == 0) {
    current_sim->set_ftrace(enabled);
  } else {
    std::cout << "Usage: trace {i|m|f} {on|off}\n";
    return 0;
  }

  current_sim->print_trace_status();
  return 0;
}

static int cmd_q(char *args) {
  (void)args;
  return -1;
}

static int cmd_help(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler)(char *);
} cmd_table[] = {
  {"help", "Display information about all supported commands", cmd_help},
  {"c", "Continue the execution of the program", cmd_c},
  {"si", "Step instruction", cmd_si},
  {"info", "Print register/watchpoint/trace information", cmd_info},
  {"x", "Examine memory", cmd_x},
  {"p", "Evaluate expression", cmd_p},
  {"w", "Set watchpoint", cmd_w},
  {"d", "Delete watchpoint", cmd_d},
  {"trace", "Toggle itrace/mtrace/ftrace", cmd_trace},
  {"q", "Exit NPC", cmd_q},
};

static constexpr int NR_CMD = sizeof(cmd_table) / sizeof(cmd_table[0]);

static int cmd_help(char *args) {
  while (args != nullptr && *args == ' ') {
    args++;
  }

  char *arg = (args == nullptr || *args == '\0') ? nullptr : std::strtok(args, " ");
  if (arg == nullptr) {
    for (int i = 0; i < NR_CMD; i++) {
      std::cout << cmd_table[i].name << " - " << cmd_table[i].description << "\n";
    }
    return 0;
  }

  for (int i = 0; i < NR_CMD; i++) {
    if (std::strcmp(arg, cmd_table[i].name) == 0) {
      std::cout << cmd_table[i].name << " - " << cmd_table[i].description << "\n";
      return 0;
    }
  }

  std::cout << "Unknown command '" << arg << "'\n";
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop(Simulator &sim) {
  current_sim = &sim;
  if (is_batch_mode) {
    cmd_c(nullptr);
    return;
  }

  init_wp_pool();

  while (true) {
    std::string line = rl_gets();
    if (!std::cin.good()) {
      break;
    }
    if (line.empty()) {
      continue;
    }

    std::string buffer = line;
    char *str = buffer.data();
    char *str_end = str + buffer.size();

    char *cmd = std::strtok(str, " ");
    if (cmd == nullptr) {
      continue;
    }

    char *args = cmd + std::strlen(cmd) + 1;
    if (args >= str_end) {
      args = nullptr;
    }

    int i = 0;
    for (; i < NR_CMD; i++) {
      if (std::strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) {
          return;
        }
        break;
      }
    }

    if (i == NR_CMD) {
      std::cout << "Unknown command '" << cmd << "'\n";
    }
  }
}

}  // namespace npc
