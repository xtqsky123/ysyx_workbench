#***************************************************************************************
# Copyright (c) 2014-2024 Zihao Yu, Nanjing University
#
# NEMU is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
#**************************************************************************************/

NAME = gen-expr
SRCS = gen-expr.c
NEMU_HOME=/home/xtq/ics2025/nemu
include $(NEMU_HOME)/scripts/build.mk

EXPR_TEST_SRCS = expr-test.c ../../src/monitor/sdb/expr.c
EXPR_TEST_BINARY = $(BUILD_DIR)/expr-test
EXPR_TEST_INC = -I$(NEMU_HOME)/include \
                -I$(NEMU_HOME)/src/isa/riscv32/include \
                -I$(NEMU_HOME)/src/monitor/sdb
EXPR_TEST_CFLAGS = -O2 -Wall -Werror $(EXPR_TEST_INC) -D__GUEST_ISA__=riscv32

.PHONY: expr-test

expr-test: $(EXPR_TEST_BINARY)

$(EXPR_TEST_BINARY): $(EXPR_TEST_SRCS)
	@echo + CC/LD $@
	@mkdir -p $(BUILD_DIR)
	@$(CC) $(EXPR_TEST_CFLAGS) -o $@ $(EXPR_TEST_SRCS)

app: $(EXPR_TEST_BINARY)
