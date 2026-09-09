/* verilator lint_off DECLFILENAME */
module integrator #(
    parameter int data_width = 8,
    parameter int acc_width  = 16
) (
    input  logic                         clk,
    input  logic                         rst,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] data,
    output logic signed [ acc_width-1:0] acc,
    output logic                         o_valid
);
    always_ff @(posedge clk) begin
        if (rst) begin
            acc <= '0;
        end else if (i_valid) begin
            acc <= acc + {{acc_width - data_width{data[data_width-1]}}, data};
        end
        o_valid <= !rst && i_valid;
    end
endmodule

module comb_filter #(
    parameter int data_width = 16,
    parameter int diff_width = 8
) (
    input  logic                         clk,
    input  logic                         rst,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] data,
    output logic signed [diff_width-1:0] diff,
    output logic                         o_valid
);
    logic [data_width-1:0] previous_data;
    logic [data_width-1:0] temp_diff;
    logic [diff_width-1:0] diff_d;
    logic                  o_valid_d;

    always_ff @(posedge clk) begin
        if (rst) begin
            previous_data <= '0;
        end else if (i_valid) begin
            previous_data <= data;
        end
        if (o_valid_d) begin
            diff <= diff_d;
        end
        o_valid <= o_valid_d;
    end

    always_comb begin
        localparam int lsb = data_width - diff_width;
        temp_diff = data - previous_data;
        diff_d    = temp_diff[data_width-1:lsb] + temp_diff[lsb-1];
        o_valid_d = !rst && i_valid;
    end
endmodule

module downsample #(
    parameter int data_width = 8,
    parameter int cnt_width  = 8
) (
    input  logic                         clk,
    input  logic                         rst,
    input  logic        [ cnt_width-1:0] rate,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] in,
    output logic signed [data_width-1:0] out,
    output logic                         o_valid
);
    logic [cnt_width-1:0] cnt;
    logic                 o_valid_d;
    counter #(
        .width(cnt_width)
    ) u_cnt (
        .clk     (clk),
        .rst     (o_valid_d || rst),
        .enable  (i_valid),
        .count   (cnt),
        /* verilator lint_off PINCONNECTEMPTY */
        .overflow()
        /* verilator lint_on PINCONNECTEMPTY */
    );

    assign o_valid_d = cnt == rate;
    always_ff @(posedge clk) begin
        if (o_valid_d) begin
            out <= in;
        end
        o_valid <= o_valid_d;
    end
endmodule


module cic_filter
    import cic_filter_param::*;
#(
    parameter int data_width = default_width,
    parameter int rate = default_rate,
    parameter int n = default_n
) (
    input  logic                         clk,
    input  logic                         rst,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] in,
    output logic signed [data_width-1:0] out,
    output logic                         o_valid
);
    // 函数只在编译时调用，因此关闭覆盖率分析
    /* verilator coverage_off */
    // 在编译时计算寄存器宽度
    // @param i 当前滤波器级联数
    function int cal_reg_width(int i);
        return data_width + $clog2(rate ** i);
    endfunction
    /* verilator coverage_on */

    generate
        if (rate < 1) begin : gen_check_rate
            $error("抽取速率必须>=1");
        end
        if (n < 1) begin : gen_check_n
            $error("滤波器阶数必须>=1");
        end
    endgenerate
    localparam int int_width = cal_reg_width(n);

    // 积分器
    generate
        logic signed [int_width-1:0] int_result_pipeline[n+1];
        logic                        int_valid_pipeline [n+1];
        for (genvar i = 0; i != n; ++i) begin : gen_u_int
            localparam int int_data_width = i == 0 ? data_width : int_width;
            integrator #(
                .data_width(i == 0 ? data_width : int_width),
                .acc_width (int_width)
            ) u_int (
                .clk    (clk),
                .rst    (rst),
                .i_valid(int_valid_pipeline[i]),
                .data   (int_result_pipeline[i][int_data_width-1:0]),
                .acc    (int_result_pipeline[i+1]),
                .o_valid(int_valid_pipeline[i+1])
            );
        end
        always_comb begin
            int_result_pipeline[0] = {{int_width - data_width{1'b0}}, in};
            int_valid_pipeline[0]  = i_valid;
        end
    endgenerate

    // 抽取器
    localparam int downsample_rate = rate - 1;
    localparam int downsample_cnt_width = $clog2(downsample_rate);
    logic signed [int_width-1:0] downsample_result;
    logic                        downsample_valid;
    downsample #(
        .data_width(int_width),
        .cnt_width (downsample_cnt_width)
    ) u_downsample (
        .clk    (clk),
        .rst    (rst),
        .rate   (downsample_rate[downsample_cnt_width-1:0]),
        .i_valid(int_valid_pipeline[n]),
        .in     (int_result_pipeline[n]),
        .out    (downsample_result),
        .o_valid(downsample_valid)
    );

    // 梳状滤波器
    generate
        logic signed [int_width-1:0] comb_filter_result_pipeline[n+1];
        logic                        comb_filter_valid_pipeline [n+1];
        for (genvar i = 0; i != n; ++i) begin : gen_u_comb_filter
            localparam int comb_filter_data_width = cal_reg_width(n - i);
            localparam int comb_filter_diff_width = cal_reg_width(n - i - 1);
            comb_filter #(
                .data_width(comb_filter_data_width),
                .diff_width(comb_filter_diff_width)
            ) u_comb_filter (
                .clk    (clk),
                .rst    (rst),
                .i_valid(comb_filter_valid_pipeline[i]),
                .data   (comb_filter_result_pipeline[i][comb_filter_data_width-1:0]),
                .diff   (comb_filter_result_pipeline[i+1][comb_filter_diff_width-1:0]),
                .o_valid(comb_filter_valid_pipeline[i+1])
            );
        end
        always_comb begin
            comb_filter_result_pipeline[0] = downsample_result;
            comb_filter_valid_pipeline[0]  = downsample_valid;
        end
    endgenerate

    // CIC滤波器输出
    always_comb begin
        out     = comb_filter_result_pipeline[n][data_width-1:0];
        o_valid = comb_filter_valid_pipeline[n];
    end
endmodule

/* verilator lint_on DECLFILENAME */
