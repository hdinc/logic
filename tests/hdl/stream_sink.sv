// the receiving side of a stream with a data width parameter, grouped into
// a bundle port s whose data width follows TDATA_WIDTH: always ready, shows
// the data while valid
module stream_sink #(
    parameter int TDATA_WIDTH = 8
) (
    input  logic                   s_tvalid,
    output logic                   s_tready,
    input  logic [TDATA_WIDTH-1:0] s_tdata,
    output logic [TDATA_WIDTH-1:0] seen
);
    assign s_tready = 1'b1;
    assign seen = s_tvalid ? s_tdata : '0;
endmodule
