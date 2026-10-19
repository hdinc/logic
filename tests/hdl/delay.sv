`timescale 1ns / 100ps
// y follows a 5 ns later, done is 1 from 20 ns
module delay (
    input  logic a,
    output logic y,
    output logic done
);
    initial begin
        y = 0;
        done = 0;
        #20 done = 1;
    end
    always @(a) y <= #5 a;
endmodule
