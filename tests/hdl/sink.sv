// the receiving side of a valid/ready handshake, grouped into a bundle port
// s by the cell: always ready, shows the data while valid
module sink (
    input  logic       s_valid,
    output logic       s_ready,
    input  logic [7:0] s_data,
    output logic [7:0] seen
);
    assign s_ready = 1'b1;
    assign seen = s_valid ? s_data : 8'h00;
endmodule
