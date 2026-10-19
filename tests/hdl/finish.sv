`timescale 1ns / 1ns
// counts nanoseconds until $finish at 10 ns
module finish (
    output logic [7:0] t
);
    initial begin
        t = 0;
        repeat (10) #1 t = t + 1;
        $finish;
    end
endmodule
