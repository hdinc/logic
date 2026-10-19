// a 100 bit adder and a 4 bit bus driver
module wide (
    input  logic [99:0] a,
    input  logic [99:0] b,
    output logic [99:0] sum,
    input  logic        oe,
    input  logic [3:0]  d,
    inout  wire  [3:0]  bus,
    output logic [3:0]  seen
);
    assign sum = a + b;
    assign bus = oe ? d : 4'bz;
    assign seen = bus;
endmodule
