// 1-bit full adder from primitive gates (zero-delay: settles in delta cycles).
module full_adder (a, b, cin, sum, cout);
  input a, b, cin;
  output sum, cout;
  wire axb, t1, t2;

  xor x1 (axb, a, b);
  xor x2 (sum, axb, cin);
  and a1 (t1, a, b);
  and a2 (t2, axb, cin);
  or  o1 (cout, t1, t2);
endmodule
