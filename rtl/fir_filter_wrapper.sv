module fir_filter_wrapper
    import fir_filter_param::*;
(
    input  logic                         clk,
    input  logic                         rst,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] in,
    output logic                         o_valid[4],
    output logic signed [data_width-1:0] out    [4]
);
    localparam int parallel_table[4] = '{parallel, parallel + 1, parallel + 2, parallel + 4};
    generate
        for (genvar i = 0; i != 4; ++i) begin : gen_fir
            fir_filter #(
                .data_width(data_width),
                .weight_width(weight_width),
                .taps(taps),
                .parallel(parallel_table[i]),
                .weights(i >= 2 ? shifted_weights : weights)
            ) u_fir (
                .clk    (clk),
                .rst    (rst),
                .i_valid(i_valid),
                .in     (in),
                .o_valid(o_valid[i]),
                .out    (out[i])
            );
        end
    endgenerate
endmodule
