module example (
  input         clk,
  input         reset,
  output reg    halt,
  output reg [31:0] pc,
  output wire [31:0] inst
);

  import "DPI-C" function int  pmem_read(input int raddr);
  import "DPI-C" function void pmem_write(input int waddr, input int wdata, input byte wmask);
  import "DPI-C" function void npc_halt(input int code, input int pc);

  localparam [31:0] RESET_VECTOR = 32'h8000_0000;
  localparam [31:0] NOP          = 32'h0000_0013;

  localparam [3:0] ALU_ADD  = 4'd0;
  localparam [3:0] ALU_SUB  = 4'd1;
  localparam [3:0] ALU_SLT  = 4'd2;
  localparam [3:0] ALU_SLTU = 4'd3;
  localparam [3:0] ALU_XOR  = 4'd4;
  localparam [3:0] ALU_OR   = 4'd5;
  localparam [3:0] ALU_AND  = 4'd6;
  localparam [3:0] ALU_SLL  = 4'd7;
  localparam [3:0] ALU_SRL  = 4'd8;
  localparam [3:0] ALU_SRA  = 4'd9;
  localparam [3:0] ALU_COPY = 4'd10;

  localparam [31:0] CSR_MVENDORID = 32'h7973_7978;
  localparam [31:0] CSR_MARCHID   = 32'd100014131;

  wire [31:0] inst_word = reset ? NOP : pmem_read(pc);
  assign inst = inst_word;

  reg         rf_wen;
  reg  [3:0]  rf_waddr;
  reg  [31:0] wb_data /* verilator public_flat */;

  wire [3:0]  ifid_rs1_idx = ifid_inst[18:15];
  wire [3:0]  ifid_rs2_idx = ifid_inst[23:20];
  wire [3:0]  rf_raddr1 = ifid_rs1_idx;
  wire [3:0]  rf_raddr2 = ifid_rs2_idx;
  wire [31:0] rs1_data;
  wire [31:0] rs2_data_rf;

  RegisterFile u_rf (
    .clk(clk),
    .wen(rf_wen),
    .waddr(rf_waddr),
    .wdata(wb_data),
    .raddr1(rf_raddr1),
    .rdata1(rs1_data),
    .raddr2(rf_raddr2),
    .rdata2(rs2_data_rf)
  );

  reg  [63:0] csr_mcycle;
  reg  [31:0] csr_mtvec;
  reg  [31:0] csr_mepc;
  reg  [31:0] csr_mcause;
  reg  [31:0] csr_mstatus;

  reg         ifid_valid;
  reg  [31:0] ifid_pc;
  reg  [31:0] ifid_inst;

  reg         idex_valid;
  reg  [31:0] idex_pc;
  reg  [31:0] idex_inst;
  reg  [31:0] idex_rs1_val;
  reg  [31:0] idex_rs2_val;
  reg  [3:0]  idex_rs1_idx;
  reg  [3:0]  idex_rs2_idx;
  reg  [3:0]  idex_rd_idx;

  reg         exmem_valid;
  reg  [31:0] exmem_pc;
  reg  [31:0] exmem_inst;
  reg  [3:0]  exmem_rd_idx;
  reg         exmem_rf_wen;
  reg  [31:0] exmem_alu_result;
  reg  [31:0] exmem_rs2_val;
  reg         exmem_is_load;
  reg  [2:0]  exmem_load_funct3;
  reg         exmem_store_en_r;
  reg  [31:0] exmem_store_addr_r;
  reg  [31:0] exmem_store_data_r;
  reg  [7:0]  exmem_store_mask_r;
  reg         exmem_wb_from_alu;
  reg         exmem_wb_from_pc4;
  reg         exmem_wb_from_csr;
  reg  [31:0] exmem_csr_rdata;
  reg         exmem_ebreak;
  reg         exmem_invalid;

  reg         memwb_valid;
  reg  [31:0] memwb_pc;
  reg  [3:0]  memwb_rd_idx;
  reg         memwb_rf_wen;
  reg  [31:0] memwb_wb_data;
  reg         memwb_ebreak;
  reg         memwb_invalid;

  reg  [31:0] load_addr /* verilator public_flat */;
  reg  [31:0] load_word /* verilator public_flat */;
  reg         store_en  /* verilator public_flat */;
  reg  [31:0] store_addr /* verilator public_flat */;
  reg  [31:0] rs2_data  /* verilator public_flat */;

  wire [31:0] mem_stage_word = (exmem_valid && exmem_is_load) ?
                               pmem_read({exmem_alu_result[31:2], 2'b00}) : 32'b0;

  reg  [3:0]  alu_op;
  reg  [31:0] alu_src1;
  reg  [31:0] alu_src2;
  wire [31:0] alu_result;

  ALU u_alu (
    .alu_op(alu_op),
    .src1(alu_src1),
    .src2(alu_src2),
    .result(alu_result)
  );

  function [31:0] load_lb;
    input [31:0] word_data;
    input [1:0]  byte_offset;
    begin
      case (byte_offset)
        2'd0: load_lb = {{24{word_data[7]}}, word_data[7:0]};
        2'd1: load_lb = {{24{word_data[15]}}, word_data[15:8]};
        2'd2: load_lb = {{24{word_data[23]}}, word_data[23:16]};
        default: load_lb = {{24{word_data[31]}}, word_data[31:24]};
      endcase
    end
  endfunction

  function [31:0] load_lbu;
    input [31:0] word_data;
    input [1:0]  byte_offset;
    begin
      case (byte_offset)
        2'd0: load_lbu = {24'b0, word_data[7:0]};
        2'd1: load_lbu = {24'b0, word_data[15:8]};
        2'd2: load_lbu = {24'b0, word_data[23:16]};
        default: load_lbu = {24'b0, word_data[31:24]};
      endcase
    end
  endfunction

  function [31:0] load_lh;
    input [31:0] word_data;
    input        half_offset;
    begin
      if (half_offset == 1'b0) load_lh = {{16{word_data[15]}}, word_data[15:0]};
      else load_lh = {{16{word_data[31]}}, word_data[31:16]};
    end
  endfunction

  function [31:0] load_lhu;
    input [31:0] word_data;
    input        half_offset;
    begin
      if (half_offset == 1'b0) load_lhu = {16'b0, word_data[15:0]};
      else load_lhu = {16'b0, word_data[31:16]};
    end
  endfunction

  function [31:0] csr_read;
    input [11:0] addr;
    begin
      case (addr)
        12'h300: csr_read = csr_mstatus;
        12'h305: csr_read = csr_mtvec;
        12'h341: csr_read = csr_mepc;
        12'h342: csr_read = csr_mcause;
        12'hb00: csr_read = csr_mcycle[31:0];
        12'hb80: csr_read = csr_mcycle[63:32];
        12'hf11: csr_read = CSR_MVENDORID;
        12'hf12: csr_read = CSR_MARCHID;
        default: csr_read = 32'b0;
      endcase
    end
  endfunction

  wire [6:0]  ex_opcode   = idex_inst[6:0];
  wire [2:0]  ex_funct3   = idex_inst[14:12];
  wire [6:0]  ex_funct7   = idex_inst[31:25];
  wire [11:0] ex_csr_addr = idex_inst[31:20];

  wire [31:0] ex_imm_i = {{20{idex_inst[31]}}, idex_inst[31:20]};
  wire [31:0] ex_imm_s = {{20{idex_inst[31]}}, idex_inst[31:25], idex_inst[11:7]};
  wire [31:0] ex_imm_b = {{19{idex_inst[31]}}, idex_inst[31], idex_inst[7], idex_inst[30:25], idex_inst[11:8], 1'b0};
  wire [31:0] ex_imm_u = {idex_inst[31:12], 12'b0};
  wire [31:0] ex_imm_j = {{11{idex_inst[31]}}, idex_inst[31], idex_inst[19:12], idex_inst[20], idex_inst[30:21], 1'b0};

  wire ex_is_lui    = (ex_opcode == 7'b0110111);
  wire ex_is_auipc  = (ex_opcode == 7'b0010111);
  wire ex_is_jal    = (ex_opcode == 7'b1101111);
  wire ex_is_jalr   = (ex_opcode == 7'b1100111) && (ex_funct3 == 3'b000);
  wire ex_is_branch = (ex_opcode == 7'b1100011);
  wire ex_is_load   = (ex_opcode == 7'b0000011);
  wire ex_is_store  = (ex_opcode == 7'b0100011);
  wire ex_is_op_imm = (ex_opcode == 7'b0010011);
  wire ex_is_op     = (ex_opcode == 7'b0110011);
  wire ex_is_system = (ex_opcode == 7'b1110011);

  wire [6:0] if_opcode = ifid_inst[6:0];
  wire [2:0] if_funct3 = ifid_inst[14:12];
  wire if_uses_rs1 = (if_opcode == 7'b0010011) ||
                     (if_opcode == 7'b0110011) ||
                     (if_opcode == 7'b0000011) ||
                     (if_opcode == 7'b0100011) ||
                     (if_opcode == 7'b1100011) ||
                     (if_opcode == 7'b1100111) ||
                     ((if_opcode == 7'b1110011) && (if_funct3 != 3'b000));
  wire if_uses_rs2 = (if_opcode == 7'b0110011) ||
                     (if_opcode == 7'b0100011) ||
                     (if_opcode == 7'b1100011);

  wire [31:0] exmem_forward_value = exmem_is_load ? mem_stage_wb_data :
                                    exmem_wb_from_pc4 ? (exmem_pc + 32'd4) :
                                    exmem_wb_from_csr ? exmem_csr_rdata :
                                    exmem_alu_result;

  wire [31:0] memwb_forward_value = memwb_wb_data;
  wire [31:0] id_rs1_value = (ifid_rs1_idx == 4'd0) ? 32'b0 :
                             (exmem_valid && exmem_rf_wen && !exmem_is_load &&
                              (exmem_rd_idx == ifid_rs1_idx)) ? exmem_forward_value :
                             (memwb_valid && memwb_rf_wen &&
                              (memwb_rd_idx == ifid_rs1_idx)) ? memwb_forward_value :
                             rs1_data;
  wire [31:0] id_rs2_value = (ifid_rs2_idx == 4'd0) ? 32'b0 :
                             (exmem_valid && exmem_rf_wen && !exmem_is_load &&
                              (exmem_rd_idx == ifid_rs2_idx)) ? exmem_forward_value :
                             (memwb_valid && memwb_rf_wen &&
                              (memwb_rd_idx == ifid_rs2_idx)) ? memwb_forward_value :
                             rs2_data_rf;
  wire load_use_hazard = ifid_valid && idex_valid && ex_is_load && (idex_rd_idx != 4'd0) &&
                         ((if_uses_rs1 && (ifid_rs1_idx == idex_rd_idx)) ||
                          (if_uses_rs2 && (ifid_rs2_idx == idex_rd_idx)));

  reg [31:0] ex_rs1_fwd;
  reg [31:0] ex_rs2_fwd;

  always @(*) begin
    ex_rs1_fwd = idex_rs1_val;
    if (exmem_valid && exmem_rf_wen && (exmem_rd_idx != 4'd0) && (exmem_rd_idx == idex_rs1_idx)) begin
      ex_rs1_fwd = exmem_forward_value;
    end
    else if (memwb_valid && memwb_rf_wen && (memwb_rd_idx != 4'd0) && (memwb_rd_idx == idex_rs1_idx)) begin
      ex_rs1_fwd = memwb_forward_value;
    end

    ex_rs2_fwd = idex_rs2_val;
    if (exmem_valid && exmem_rf_wen && (exmem_rd_idx != 4'd0) && (exmem_rd_idx == idex_rs2_idx)) begin
      ex_rs2_fwd = exmem_forward_value;
    end
    else if (memwb_valid && memwb_rf_wen && (memwb_rd_idx != 4'd0) && (memwb_rd_idx == idex_rs2_idx)) begin
      ex_rs2_fwd = memwb_forward_value;
    end
  end

  reg         ex_rf_wen;
  reg  [3:0]  ex_rd_idx;
  reg         ex_invalid;
  reg         ex_ebreak;
  reg         ex_ecall;
  reg         ex_mret;
  reg         ex_redirect_valid;
  reg  [31:0] ex_redirect_pc;
  reg         ex_csr_we;
  reg  [31:0] ex_csr_wdata;
  reg  [31:0] ex_csr_rdata;
  reg         ex_store_en;
  reg  [31:0] ex_store_addr;
  reg  [31:0] ex_store_data;
  reg  [7:0]  ex_store_mask;
  reg         ex_wb_from_alu;
  reg         ex_wb_from_pc4;
  reg         ex_wb_from_csr;
  reg         ex_is_load_r;
  reg  [2:0]  ex_load_funct3;
  reg  [31:0] ex_result;

  always @(*) begin
    alu_op = ALU_ADD;
    alu_src1 = ex_rs1_fwd;
    alu_src2 = ex_rs2_fwd;

    ex_rf_wen = 1'b0;
    ex_rd_idx = idex_rd_idx;
    ex_invalid = 1'b0;
    ex_ebreak = 1'b0;
    ex_ecall = 1'b0;
    ex_mret = 1'b0;
    ex_redirect_valid = 1'b0;
    ex_redirect_pc = 32'b0;
    ex_csr_we = 1'b0;
    ex_csr_wdata = 32'b0;
    ex_csr_rdata = csr_read(ex_csr_addr);
    ex_store_en = 1'b0;
    ex_store_addr = 32'b0;
    ex_store_data = 32'b0;
    ex_store_mask = 8'b0;
    ex_wb_from_alu = 1'b0;
    ex_wb_from_pc4 = 1'b0;
    ex_wb_from_csr = 1'b0;
    ex_is_load_r = 1'b0;
    ex_load_funct3 = ex_funct3;
    ex_result = 32'b0;

    if (!idex_valid) begin
      ex_result = 32'b0;
    end
    else if (ex_is_lui) begin
      alu_op = ALU_COPY;
      alu_src1 = 32'b0;
      alu_src2 = ex_imm_u;
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_alu = 1'b1;
      ex_result = alu_result;
    end
    else if (ex_is_auipc) begin
      alu_op = ALU_ADD;
      alu_src1 = idex_pc;
      alu_src2 = ex_imm_u;
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_alu = 1'b1;
      ex_result = alu_result;
    end
    else if (ex_is_op_imm) begin
      alu_src1 = ex_rs1_fwd;
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_alu = 1'b1;
      case (ex_funct3)
        3'b000: begin alu_op = ALU_ADD;  alu_src2 = ex_imm_i; end
        3'b010: begin alu_op = ALU_SLT;  alu_src2 = ex_imm_i; end
        3'b011: begin alu_op = ALU_SLTU; alu_src2 = ex_imm_i; end
        3'b100: begin alu_op = ALU_XOR;  alu_src2 = ex_imm_i; end
        3'b110: begin alu_op = ALU_OR;   alu_src2 = ex_imm_i; end
        3'b111: begin alu_op = ALU_AND;  alu_src2 = ex_imm_i; end
        3'b001: begin
          if (ex_funct7 == 7'b0000000) begin
            alu_op = ALU_SLL;
            alu_src2 = {27'b0, idex_inst[24:20]};
          end else begin
            ex_invalid = 1'b1;
          end
        end
        3'b101: begin
          if (ex_funct7 == 7'b0000000) begin
            alu_op = ALU_SRL;
            alu_src2 = {27'b0, idex_inst[24:20]};
          end
          else if (ex_funct7 == 7'b0100000) begin
            alu_op = ALU_SRA;
            alu_src2 = {27'b0, idex_inst[24:20]};
          end
          else begin
            ex_invalid = 1'b1;
          end
        end
        default: ex_invalid = 1'b1;
      endcase
      ex_result = alu_result;
    end
    else if (ex_is_op) begin
      alu_src1 = ex_rs1_fwd;
      alu_src2 = ex_rs2_fwd;
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_alu = 1'b1;
      case (ex_funct3)
        3'b000: begin
          if (ex_funct7 == 7'b0000000) alu_op = ALU_ADD;
          else if (ex_funct7 == 7'b0100000) alu_op = ALU_SUB;
          else ex_invalid = 1'b1;
        end
        3'b001: if (ex_funct7 == 7'b0000000) alu_op = ALU_SLL; else ex_invalid = 1'b1;
        3'b010: if (ex_funct7 == 7'b0000000) alu_op = ALU_SLT; else ex_invalid = 1'b1;
        3'b011: if (ex_funct7 == 7'b0000000) alu_op = ALU_SLTU; else ex_invalid = 1'b1;
        3'b100: if (ex_funct7 == 7'b0000000) alu_op = ALU_XOR; else ex_invalid = 1'b1;
        3'b101: begin
          if (ex_funct7 == 7'b0000000) alu_op = ALU_SRL;
          else if (ex_funct7 == 7'b0100000) alu_op = ALU_SRA;
          else ex_invalid = 1'b1;
        end
        3'b110: if (ex_funct7 == 7'b0000000) alu_op = ALU_OR; else ex_invalid = 1'b1;
        3'b111: if (ex_funct7 == 7'b0000000) alu_op = ALU_AND; else ex_invalid = 1'b1;
        default: ex_invalid = 1'b1;
      endcase
      ex_result = alu_result;
    end
    else if (ex_is_jal) begin
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_pc4 = 1'b1;
      ex_redirect_valid = 1'b1;
      ex_redirect_pc = idex_pc + ex_imm_j;
      ex_result = idex_pc + 32'd4;
    end
    else if (ex_is_jalr) begin
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_wb_from_pc4 = 1'b1;
      ex_redirect_valid = 1'b1;
      ex_redirect_pc = (ex_rs1_fwd + ex_imm_i) & ~32'd1;
      ex_result = idex_pc + 32'd4;
    end
    else if (ex_is_branch) begin
      case (ex_funct3)
        3'b000: if (ex_rs1_fwd == ex_rs2_fwd) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        3'b001: if (ex_rs1_fwd != ex_rs2_fwd) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        3'b100: if ($signed(ex_rs1_fwd) < $signed(ex_rs2_fwd)) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        3'b101: if ($signed(ex_rs1_fwd) >= $signed(ex_rs2_fwd)) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        3'b110: if (ex_rs1_fwd < ex_rs2_fwd) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        3'b111: if (ex_rs1_fwd >= ex_rs2_fwd) begin ex_redirect_valid = 1'b1; ex_redirect_pc = idex_pc + ex_imm_b; end
        default: ex_invalid = 1'b1;
      endcase
    end
    else if (ex_is_load) begin
      ex_rf_wen = (idex_rd_idx != 4'd0);
      ex_is_load_r = 1'b1;
      ex_store_addr = ex_rs1_fwd + ex_imm_i;
      ex_result = ex_store_addr;
      case (ex_funct3)
        3'b000: ;
        3'b001: if (ex_store_addr[0] != 1'b0) ex_invalid = 1'b1;
        3'b010: if (ex_store_addr[1:0] != 2'b00) ex_invalid = 1'b1;
        3'b100: ;
        3'b101: if (ex_store_addr[0] != 1'b0) ex_invalid = 1'b1;
        default: ex_invalid = 1'b1;
      endcase
    end
    else if (ex_is_store) begin
      ex_store_en = 1'b1;
      ex_store_addr = ex_rs1_fwd + ex_imm_s;
      ex_result = ex_store_addr;
      case (ex_funct3)
        3'b000: begin
          ex_store_mask = 8'b0000_0001 << ex_store_addr[1:0];
          ex_store_data = ex_rs2_fwd << (ex_store_addr[1:0] * 8);
        end
        3'b001: begin
          if (ex_store_addr[0] == 1'b0) begin
            ex_store_mask = ex_store_addr[1] ? 8'b0000_1100 : 8'b0000_0011;
            ex_store_data = ex_rs2_fwd << (ex_store_addr[1] * 16);
          end else begin
            ex_store_en = 1'b0;
            ex_invalid = 1'b1;
          end
        end
        3'b010: begin
          if (ex_store_addr[1:0] == 2'b00) begin
            ex_store_mask = 8'b0000_1111;
            ex_store_data = ex_rs2_fwd;
          end else begin
            ex_store_en = 1'b0;
            ex_invalid = 1'b1;
          end
        end
        default: begin
          ex_store_en = 1'b0;
          ex_invalid = 1'b1;
        end
      endcase
    end
    else if (ex_is_system) begin
      if (idex_inst == 32'h0010_0073) begin
        ex_ebreak = 1'b1;
      end
      else if (idex_inst == 32'h0000_0073) begin
        ex_ecall = 1'b1;
        ex_redirect_valid = 1'b1;
        ex_redirect_pc = csr_mtvec;
      end
      else if (idex_inst == 32'h3020_0073) begin
        ex_mret = 1'b1;
        ex_redirect_valid = 1'b1;
        ex_redirect_pc = csr_mepc;
      end
      else if (ex_funct3 == 3'b001) begin
        ex_rf_wen = (idex_rd_idx != 4'd0);
        ex_wb_from_csr = 1'b1;
        ex_csr_we = 1'b1;
        ex_csr_wdata = ex_rs1_fwd;
        ex_result = ex_csr_rdata;
      end
      else if (ex_funct3 == 3'b010) begin
        ex_rf_wen = (idex_rd_idx != 4'd0);
        ex_wb_from_csr = 1'b1;
        if (idex_rs1_idx != 4'd0) begin
          ex_csr_we = 1'b1;
          ex_csr_wdata = ex_csr_rdata | ex_rs1_fwd;
        end
        ex_result = ex_csr_rdata;
      end
      else begin
        ex_invalid = 1'b1;
      end
    end
    else begin
      ex_invalid = 1'b1;
    end
  end

  reg [31:0] mem_stage_wb_data;
  reg        mem_stage_invalid;

  always @(*) begin
    mem_stage_wb_data = exmem_alu_result;
    mem_stage_invalid = exmem_invalid;

    if (exmem_is_load) begin
      case (exmem_load_funct3)
        3'b000: mem_stage_wb_data = load_lb(mem_stage_word, exmem_alu_result[1:0]);
        3'b001: begin
          if (exmem_alu_result[0] == 1'b0) mem_stage_wb_data = load_lh(mem_stage_word, exmem_alu_result[1]);
          else mem_stage_invalid = 1'b1;
        end
        3'b010: begin
          if (exmem_alu_result[1:0] == 2'b00) mem_stage_wb_data = mem_stage_word;
          else mem_stage_invalid = 1'b1;
        end
        3'b100: mem_stage_wb_data = load_lbu(mem_stage_word, exmem_alu_result[1:0]);
        3'b101: begin
          if (exmem_alu_result[0] == 1'b0) mem_stage_wb_data = load_lhu(mem_stage_word, exmem_alu_result[1]);
          else mem_stage_invalid = 1'b1;
        end
        default: mem_stage_invalid = 1'b1;
      endcase
    end
    else if (exmem_wb_from_pc4) begin
      mem_stage_wb_data = exmem_pc + 32'd4;
    end
    else if (exmem_wb_from_csr) begin
      mem_stage_wb_data = exmem_csr_rdata;
    end
    else begin
      mem_stage_wb_data = exmem_alu_result;
    end
  end

  always @(*) begin
    rf_wen = memwb_valid && memwb_rf_wen && !halt;
    rf_waddr = memwb_rd_idx;
    wb_data = memwb_wb_data;

    load_addr = exmem_alu_result;
    load_word = mem_stage_word;
    store_en = exmem_valid && exmem_store_en_r;
    store_addr = exmem_store_addr_r;
    rs2_data = exmem_rs2_val;
  end

  always @(posedge clk) begin
    if (reset) begin
      pc <= RESET_VECTOR;
      halt <= 1'b0;
      csr_mcycle <= 64'b0;
      csr_mtvec <= 32'b0;
      csr_mepc <= 32'b0;
      csr_mcause <= 32'b0;
      csr_mstatus <= 32'h0000_1800;

      ifid_valid <= 1'b0;
      ifid_pc <= 32'b0;
      ifid_inst <= NOP;

      idex_valid <= 1'b0;
      idex_pc <= 32'b0;
      idex_inst <= NOP;
      idex_rs1_val <= 32'b0;
      idex_rs2_val <= 32'b0;
      idex_rs1_idx <= 4'd0;
      idex_rs2_idx <= 4'd0;
      idex_rd_idx <= 4'd0;

      exmem_valid <= 1'b0;
      exmem_pc <= 32'b0;
      exmem_inst <= NOP;
      exmem_rd_idx <= 4'd0;
      exmem_rf_wen <= 1'b0;
      exmem_alu_result <= 32'b0;
      exmem_rs2_val <= 32'b0;
      exmem_is_load <= 1'b0;
      exmem_load_funct3 <= 3'b000;
      exmem_store_en_r <= 1'b0;
      exmem_store_addr_r <= 32'b0;
      exmem_store_data_r <= 32'b0;
      exmem_store_mask_r <= 8'b0;
      exmem_wb_from_alu <= 1'b0;
      exmem_wb_from_pc4 <= 1'b0;
      exmem_wb_from_csr <= 1'b0;
      exmem_csr_rdata <= 32'b0;
      exmem_ebreak <= 1'b0;
      exmem_invalid <= 1'b0;

      memwb_valid <= 1'b0;
      memwb_pc <= 32'b0;
      memwb_rd_idx <= 4'd0;
      memwb_rf_wen <= 1'b0;
      memwb_wb_data <= 32'b0;
      memwb_ebreak <= 1'b0;
      memwb_invalid <= 1'b0;
    end
    else if (!halt) begin
      csr_mcycle <= csr_mcycle + 64'd1;

      if (idex_valid && ex_csr_we) begin
        case (ex_csr_addr)
          12'h300: csr_mstatus <= ex_csr_wdata;
          12'h305: csr_mtvec <= ex_csr_wdata;
          12'h341: csr_mepc <= ex_csr_wdata;
          12'h342: csr_mcause <= ex_csr_wdata;
          12'hb00: csr_mcycle[31:0] <= ex_csr_wdata;
          12'hb80: csr_mcycle[63:32] <= ex_csr_wdata;
          default: ;
        endcase
      end

      if (idex_valid && ex_ecall) begin
        csr_mepc <= idex_pc;
        csr_mcause <= 32'd11;
      end

      if (exmem_valid && exmem_store_en_r) begin
        pmem_write(exmem_store_addr_r, exmem_store_data_r, exmem_store_mask_r);
      end

      if (memwb_valid && memwb_invalid) begin
        halt <= 1'b1;
        npc_halt(32'd1, memwb_pc);
      end
      else if (memwb_valid && memwb_ebreak) begin
        halt <= 1'b1;
        npc_halt(u_rf.rf[10], memwb_pc);
      end
      else begin
        if (ex_redirect_valid) begin
          pc <= ex_redirect_pc;
        end
        else if (load_use_hazard) begin
          pc <= pc;
        end
        else begin
          pc <= pc + 32'd4;
        end

        memwb_valid <= exmem_valid;
        memwb_pc <= exmem_pc;
        memwb_rd_idx <= exmem_rd_idx;
        memwb_rf_wen <= exmem_rf_wen;
        memwb_wb_data <= mem_stage_wb_data;
        memwb_ebreak <= exmem_ebreak;
        memwb_invalid <= mem_stage_invalid;

        exmem_valid <= idex_valid;
        exmem_pc <= idex_pc;
        exmem_inst <= idex_inst;
        exmem_rd_idx <= ex_rd_idx;
        exmem_rf_wen <= ex_rf_wen;
        exmem_alu_result <= ex_result;
        exmem_rs2_val <= ex_rs2_fwd;
        exmem_is_load <= ex_is_load_r;
        exmem_load_funct3 <= ex_load_funct3;
        exmem_store_en_r <= ex_store_en;
        exmem_store_addr_r <= ex_store_addr;
        exmem_store_data_r <= ex_store_data;
        exmem_store_mask_r <= ex_store_mask;
        exmem_wb_from_alu <= ex_wb_from_alu;
        exmem_wb_from_pc4 <= ex_wb_from_pc4;
        exmem_wb_from_csr <= ex_wb_from_csr;
        exmem_csr_rdata <= ex_csr_rdata;
        exmem_ebreak <= ex_ebreak;
        exmem_invalid <= ex_invalid;

        if (ex_redirect_valid) begin
          idex_valid <= 1'b0;
          idex_pc <= 32'b0;
          idex_inst <= NOP;
          idex_rs1_val <= 32'b0;
          idex_rs2_val <= 32'b0;
          idex_rs1_idx <= 4'd0;
          idex_rs2_idx <= 4'd0;
          idex_rd_idx <= 4'd0;

          ifid_valid <= 1'b0;
          ifid_pc <= 32'b0;
          ifid_inst <= NOP;
        end
        else if (load_use_hazard) begin
          idex_valid <= 1'b0;
          idex_pc <= 32'b0;
          idex_inst <= NOP;
          idex_rs1_val <= 32'b0;
          idex_rs2_val <= 32'b0;
          idex_rs1_idx <= 4'd0;
          idex_rs2_idx <= 4'd0;
          idex_rd_idx <= 4'd0;

          ifid_valid <= ifid_valid;
          ifid_pc <= ifid_pc;
          ifid_inst <= ifid_inst;
        end
        else begin
          idex_valid <= ifid_valid;
          idex_pc <= ifid_pc;
          idex_inst <= ifid_inst;
          idex_rs1_val <= id_rs1_value;
          idex_rs2_val <= id_rs2_value;
          idex_rs1_idx <= ifid_rs1_idx;
          idex_rs2_idx <= ifid_rs2_idx;
          idex_rd_idx <= ifid_inst[10:7];

          ifid_valid <= 1'b1;
          ifid_pc <= pc;
          ifid_inst <= inst_word;
        end
      end
    end
  end

endmodule
