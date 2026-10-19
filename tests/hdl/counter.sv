// counts rising clock edges while en is 1, rst (synchronous) clears it
module counter #(
    parameter int WIDTH = 8
) (
    input  logic             clk,
    input  logic             rst,
    input  logic             en,
    output logic [WIDTH-1:0] q
);
    always_ff @(posedge clk)
        if (rst) q <= '0;
        else if (en) q <= q + 1'b1;
endmodule
