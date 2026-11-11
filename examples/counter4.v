// 4-bit synchronous binary counter with enable and asynchronous reset.
// Each flip-flop toggles when enable and all lower bits are 1: q[i] <= q[i] ^ carry[i].
// Gates have a 1-unit delay and flip-flops a 2-unit clock-to-Q delay, so the waveform shows the
// ripple through the carry chain after each clock edge.
module counter4 (clk, rst, en, q);
  input clk, rst, en;
  output [3:0] q;
  wire [3:0] d;
  wire c1, c2, c3;

  // carry chain: carry[0] = en, carry[i] = carry[i-1] & q[i-1]
  and #1 a1 (c1, en, q[0]);
  and #1 a2 (c2, c1, q[1]);
  and #1 a3 (c3, c2, q[2]);

  xor #1 x0 (d[0], q[0], en);
  xor #1 x1 (d[1], q[1], c1);
  xor #1 x2 (d[2], q[2], c2);
  xor #1 x3 (d[3], q[3], c3);

  dff #2 f0 (.Q(q[0]), .D(d[0]), .CLK(clk), .RST(rst));
  dff #2 f1 (.Q(q[1]), .D(d[1]), .CLK(clk), .RST(rst));
  dff #2 f2 (.Q(q[2]), .D(d[2]), .CLK(clk), .RST(rst));
  dff #2 f3 (.Q(q[3]), .D(d[3]), .CLK(clk), .RST(rst));
endmodule
