#ifndef __TRACE_H__
#define __TRACE_H__

#include <common.h>

void init_ftrace(const char *elf_file, const char *img_file);

void mtrace_read(vaddr_t addr, int len, word_t data);
void mtrace_write(vaddr_t addr, int len, word_t data);

void ftrace_call(vaddr_t pc, vaddr_t target);
void ftrace_ret(vaddr_t pc, vaddr_t target);

#endif
