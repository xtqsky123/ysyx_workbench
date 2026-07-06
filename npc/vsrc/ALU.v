module ALU (
  input  [3:0]  alu_op,
  input  [31:0] src1,
  input  [31:0] src2,
  output reg [31:0] result
);

  localparam ALU_ADD  = 4'd0;
  localparam ALU_SUB  = 4'd1;
  localparam ALU_SLT  = 4'd2;
  localparam ALU_SLTU = 4'd3;
  localparam ALU_XOR  = 4'd4;
  localparam ALU_OR   = 4'd5;
  localparam ALU_AND  = 4'd6;
  localparam ALU_SLL  = 4'd7;
  localparam ALU_SRL  = 4'd8;
  localparam ALU_SRA  = 4'd9;
  localparam ALU_COPY = 4'd10;

  always @(*) begin
    case (alu_op)
      ALU_ADD:  result = src1 + src2;
      ALU_SUB:  result = src1 - src2;
      ALU_SLT:  result = ($signed(src1) < $signed(src2)) ? 32'd1 : 32'd0;
      ALU_SLTU: result = (src1 < src2) ? 32'd1 : 32'd0;
      ALU_XOR:  result = src1 ^ src2;
      ALU_OR:   result = src1 | src2;
      ALU_AND:  result = src1 & src2;
      ALU_SLL:  result = src1 << src2[4:0];
      ALU_SRL:  result = src1 >> src2[4:0];
      ALU_SRA:  result = $signed(src1) >>> src2[4:0];
      ALU_COPY: result = src2;
      default:  result = 32'b0;
    endcase
  end

endmodule
