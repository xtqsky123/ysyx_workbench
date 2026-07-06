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

#include <trace.h>
#include <isa.h>
#include <utils.h>
#include <elf.h>

#ifdef CONFIG_FTRACE
typedef struct {
  vaddr_t start;
  vaddr_t end;
  char *name;
} FuncSymbol;

static FuncSymbol *func_symbols = NULL;
static size_t nr_func_symbols = 0;
static int ftrace_depth = 0;

static char *dup_string(const char *s) {
  size_t len = strlen(s) + 1;
  char *ret = malloc(len);
  Assert(ret != NULL, "Failed to allocate memory for symbol name");
  memcpy(ret, s, len);
  return ret;
}

static const char *find_func_name(vaddr_t addr) {
  for (size_t i = 0; i < nr_func_symbols; i++) {
    if ((addr >= func_symbols[i].start && addr < func_symbols[i].end) ||
        (func_symbols[i].start == addr && func_symbols[i].end == addr)) {
      return func_symbols[i].name;
    }
  }
  return NULL;
}

static void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    log_write("  ");
  }
}

static char *infer_elf_path(const char *img_file) {
  if (img_file == NULL) return NULL;
  const char *suffix = strrchr(img_file, '.');
  if (suffix == NULL || strcmp(suffix, ".bin") != 0) return NULL;
  size_t prefix_len = suffix - img_file;
  char *elf_path = malloc(prefix_len + 5);
  Assert(elf_path != NULL, "Failed to allocate memory for elf path");
  memcpy(elf_path, img_file, prefix_len);
  memcpy(elf_path + prefix_len, ".elf", 5);
  return elf_path;
}

static void load_func_symbols_32(FILE *fp, const Elf32_Ehdr *ehdr) {
  Elf32_Shdr *shdrs = malloc(ehdr->e_shentsize * ehdr->e_shnum);
  Assert(shdrs != NULL, "Failed to allocate section headers");
  fseek(fp, ehdr->e_shoff, SEEK_SET);
  Assert(fread(shdrs, ehdr->e_shentsize, ehdr->e_shnum, fp) == ehdr->e_shnum, "Failed to read section headers");

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdrs[i].sh_type != SHT_SYMTAB) continue;
    Elf32_Shdr symtab = shdrs[i];
    Elf32_Shdr strtab = shdrs[symtab.sh_link];
    char *strbuf = malloc(strtab.sh_size);
    Assert(strbuf != NULL, "Failed to allocate string table");
    fseek(fp, strtab.sh_offset, SEEK_SET);
    Assert(fread(strbuf, 1, strtab.sh_size, fp) == strtab.sh_size, "Failed to read string table");

    size_t nr_syms = symtab.sh_size / sizeof(Elf32_Sym);
    Elf32_Sym *syms = malloc(symtab.sh_size);
    Assert(syms != NULL, "Failed to allocate symbol table");
    fseek(fp, symtab.sh_offset, SEEK_SET);
    Assert(fread(syms, sizeof(Elf32_Sym), nr_syms, fp) == nr_syms, "Failed to read symbol table");

    for (size_t j = 0; j < nr_syms; j++) {
      if (ELF32_ST_TYPE(syms[j].st_info) != STT_FUNC || syms[j].st_name == 0) continue;
      func_symbols = realloc(func_symbols, (nr_func_symbols + 1) * sizeof(FuncSymbol));
      Assert(func_symbols != NULL, "Failed to grow function symbol table");
      func_symbols[nr_func_symbols].start = syms[j].st_value;
      func_symbols[nr_func_symbols].end = syms[j].st_size == 0 ? syms[j].st_value : syms[j].st_value + syms[j].st_size;
      func_symbols[nr_func_symbols].name = dup_string(strbuf + syms[j].st_name);
      nr_func_symbols++;
    }

    free(syms);
    free(strbuf);
    break;
  }

  free(shdrs);
}

static void load_func_symbols_64(FILE *fp, const Elf64_Ehdr *ehdr) {
  Elf64_Shdr *shdrs = malloc(ehdr->e_shentsize * ehdr->e_shnum);
  Assert(shdrs != NULL, "Failed to allocate section headers");
  fseek(fp, ehdr->e_shoff, SEEK_SET);
  Assert(fread(shdrs, ehdr->e_shentsize, ehdr->e_shnum, fp) == ehdr->e_shnum, "Failed to read section headers");

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdrs[i].sh_type != SHT_SYMTAB) continue;
    Elf64_Shdr symtab = shdrs[i];
    Elf64_Shdr strtab = shdrs[symtab.sh_link];
    char *strbuf = malloc(strtab.sh_size);
    Assert(strbuf != NULL, "Failed to allocate string table");
    fseek(fp, strtab.sh_offset, SEEK_SET);
    Assert(fread(strbuf, 1, strtab.sh_size, fp) == strtab.sh_size, "Failed to read string table");

    size_t nr_syms = symtab.sh_size / sizeof(Elf64_Sym);
    Elf64_Sym *syms = malloc(symtab.sh_size);
    Assert(syms != NULL, "Failed to allocate symbol table");
    fseek(fp, symtab.sh_offset, SEEK_SET);
    Assert(fread(syms, sizeof(Elf64_Sym), nr_syms, fp) == nr_syms, "Failed to read symbol table");

    for (size_t j = 0; j < nr_syms; j++) {
      if (ELF64_ST_TYPE(syms[j].st_info) != STT_FUNC || syms[j].st_name == 0) continue;
      func_symbols = realloc(func_symbols, (nr_func_symbols + 1) * sizeof(FuncSymbol));
      Assert(func_symbols != NULL, "Failed to grow function symbol table");
      func_symbols[nr_func_symbols].start = syms[j].st_value;
      func_symbols[nr_func_symbols].end = syms[j].st_size == 0 ? syms[j].st_value : syms[j].st_value + syms[j].st_size;
      func_symbols[nr_func_symbols].name = dup_string(strbuf + syms[j].st_name);
      nr_func_symbols++;
    }

    free(syms);
    free(strbuf);
    break;
  }

  free(shdrs);
}
#endif

void init_ftrace(const char *elf_file, const char *img_file) {
#ifndef CONFIG_FTRACE
  (void)elf_file;
  (void)img_file;
  return;
#else
  char *inferred = NULL;
  const char *path = elf_file;
  if (path == NULL) {
    inferred = infer_elf_path(img_file);
    path = inferred;
  }
  if (path == NULL) {
    Log("FTrace is enabled, but no ELF file is provided");
    return;
  }

  FILE *fp = fopen(path, "rb");
  Assert(fp != NULL, "Can not open ELF file '%s' for ftrace", path);

  unsigned char ident[EI_NIDENT];
  Assert(fread(ident, 1, EI_NIDENT, fp) == EI_NIDENT, "Failed to read ELF ident");
  Assert(memcmp(ident, ELFMAG, SELFMAG) == 0, "'%s' is not a valid ELF file", path);
  rewind(fp);

  if (ident[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr ehdr;
    Assert(fread(&ehdr, 1, sizeof(ehdr), fp) == sizeof(ehdr), "Failed to read ELF32 header");
    load_func_symbols_32(fp, &ehdr);
  } else if (ident[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr ehdr;
    Assert(fread(&ehdr, 1, sizeof(ehdr), fp) == sizeof(ehdr), "Failed to read ELF64 header");
    load_func_symbols_64(fp, &ehdr);
  } else {
    panic("Unsupported ELF class in '%s'", path);
  }

  fclose(fp);
  Log("FTrace loaded %zu function symbols from %s", nr_func_symbols, path);
  free(inferred);
#endif
}

void mtrace_read(vaddr_t addr, int len, word_t data) {
#ifdef CONFIG_MTRACE
  if (nemu_state.state == NEMU_RUNNING) {
    log_write("MTRACE: read  addr=" FMT_WORD " len=%d data=" FMT_WORD " pc=" FMT_WORD "\n",
        addr, len, data, cpu.pc);
  }
#endif
}

void mtrace_write(vaddr_t addr, int len, word_t data) {
#ifdef CONFIG_MTRACE
  if (nemu_state.state == NEMU_RUNNING) {
    log_write("MTRACE: write addr=" FMT_WORD " len=%d data=" FMT_WORD " pc=" FMT_WORD "\n",
        addr, len, data, cpu.pc);
  }
#endif
}

void ftrace_call(vaddr_t pc, vaddr_t target) {
#ifdef CONFIG_FTRACE
  const char *name = find_func_name(target);
  print_indent(ftrace_depth);
  log_write("FTRACE: call " FMT_WORD " -> " FMT_WORD " <%s>\n",
      pc, target, name ? name : "???");
  ftrace_depth++;
#endif
}

void ftrace_ret(vaddr_t pc, vaddr_t target) {
#ifdef CONFIG_FTRACE
  if (ftrace_depth > 0) ftrace_depth--;
  const char *name = find_func_name(target);
  print_indent(ftrace_depth);
  log_write("FTRACE: ret  " FMT_WORD " -> " FMT_WORD " <%s>\n",
      pc, target, name ? name : "???");
#endif
}
