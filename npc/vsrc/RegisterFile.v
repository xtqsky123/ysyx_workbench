module RegisterFile (
  input         clk,
  input         wen,
  input  [3:0]  waddr,
  input  [31:0] wdata,
  input  [3:0]  raddr1,
  output [31:0] rdata1,
  input  [3:0]  raddr2,
  output [31:0] rdata2
);

  reg [31:0] rf [0:15];
  integer i;

  initial begin
    for (i = 0; i < 16; i = i + 1) begin
      rf[i] = 32'b0;
    end
  end

  always @(posedge clk) begin
    if (wen && (waddr != 4'd0)) begin
      rf[waddr] <= wdata;
    end
    rf[0] <= 32'b0;
  end

  assign rdata1 = (raddr1 == 4'd0) ? 32'b0 : rf[raddr1];
  assign rdata2 = (raddr2 == 4'd0) ? 32'b0 : rf[raddr2];

endmodule
