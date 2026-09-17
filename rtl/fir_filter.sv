/* verilator lint_off DECLFILENAME */
module mac_unit #(
    parameter int data_width = 16,
    parameter int weight_width = 16,
    parameter int acc_width = 48
) (
    input  logic                           clk,
    input  logic                           rst,
    input  logic                           clear,
    input  logic                           i_valid,
    input  logic signed [  data_width-1:0] data,
    input  logic signed [weight_width-1:0] weight,
    output logic signed [   acc_width-1:0] acc
);
    localparam int mul_width = data_width + weight_width;
    logic                        clear_buffer;
    logic                        mul_valid;
    logic signed [mul_width-1:0] mul;

    always_ff @(posedge clk) begin
        if (rst) begin
            clear_buffer <= 0;
            mul_valid    <= 0;
            acc          <= 0;
        end else begin
            mul_valid    <= i_valid;
            clear_buffer <= clear;
            if (mul_valid) begin
                acc <= (clear_buffer ? 0 : acc) + {{acc_width - mul_width{mul[mul_width-1]}}, mul};
            end else if (clear_buffer) begin
                acc <= 0;
            end
        end

        if (i_valid) begin
            mul <= data * weight;
        end
    end
endmodule

module pre_add_mac_unit #(
    parameter int data_width = 16,
    parameter int weight_width = 16,
    parameter int acc_width = 48
) (
    input  logic                           clk,
    input  logic                           rst,
    input  logic                           clear,
    input  logic                           i_valid,
    input  logic signed [  data_width-1:0] data1,
    input  logic signed [  data_width-1:0] data2,
    input  logic signed [weight_width-1:0] weight,
    output logic signed [   acc_width-1:0] acc
);
    localparam int pre_add_width = data_width + 1;
    logic                            clear_buffer;
    logic                            pre_add_valid;
    logic signed [pre_add_width-1:0] pre_add;
    logic signed [ weight_width-1:0] weight_buffer;

    mac_unit #(
        .data_width(pre_add_width),
        .weight_width(weight_width),
        .acc_width(acc_width)
    ) u_mac_unit (
        .clk    (clk),
        .rst    (rst),
        .clear  (clear_buffer),
        .i_valid(pre_add_valid),
        .data   (pre_add),
        .weight (weight_buffer),
        .acc    (acc)
    );

    always_ff @(posedge clk) begin
        if (rst) begin
            clear_buffer  <= 0;
            pre_add_valid <= 0;
        end else begin
            pre_add_valid <= i_valid;
            clear_buffer  <= clear;
            if (i_valid) begin
                weight_buffer <= weight;
                pre_add       <= data1 + data2;
            end
        end
    end
endmodule

module add_tree #(
    parameter int width = 48,
    parameter int n = 4
) (
    input  logic                              clk,
    input  logic                              rst,
    input  logic                              i_valid,
    input  logic signed [          width-1:0] data   [n],
    output logic signed [width+$clog2(n)-1:0] acc,
    output logic                              o_valid
);
    localparam int depth = $clog2(n);
    localparam int acc_width = width + depth;

    /* verilator coverage_off */
    function automatic int get_acc_num(input int target_depth);
        int num = n;
        for (int i = 0; i != target_depth; ++i) begin
            num = (num + 1) / 2;
        end
        return num;
    endfunction
    /* verilator coverage_on */

    generate
        logic                        valid_pipeline[  depth];
        logic signed [acc_width-1:0] acc_pipeline  [depth+1] [n];
        for (genvar i = 0; i != depth; ++i) begin : gen_add_tree
            localparam int cur_acc_num = get_acc_num(i);
            localparam int next_acc_num = get_acc_num(i + 1);
            localparam int cur_width = width + i;
            localparam int next_width = cur_width + 1;
            logic [next_width-1:0] acc_result[next_acc_num];

            always_comb begin
                for (int j = 0; j != next_acc_num; ++j) begin
                    if (2 * j + 1 == cur_acc_num) begin
                        acc_result[j] = {acc_pipeline[i][2*j][cur_width-1], acc_pipeline[i][2*j][cur_width-1:0]};
                    end else begin
                        acc_result[j] = $signed(acc_pipeline[i][2*j][cur_width-1:0]) + $signed(acc_pipeline[i][2*j+1][cur_width-1:0]);
                    end
                end
            end

            always_ff @(posedge clk) begin
                for (int j = 0; j != next_acc_num; ++j) begin
                    acc_pipeline[i+1][j] <= {{acc_width - next_width{acc_result[j][next_width-1]}}, acc_result[j]};
                end
            end
        end

        always_ff @(posedge clk) begin
            if (rst) begin
                valid_pipeline <= '{default: 0};
            end else begin
                valid_pipeline[0] <= i_valid;
                for (int i = 0; i != depth - 1; ++i) begin
                    valid_pipeline[i+1] <= valid_pipeline[i];
                end
            end
        end

        always_comb begin
            foreach (data[i]) begin
                acc_pipeline[0][i] = {{acc_width - width{data[i][width-1]}}, data[i]};
            end
            o_valid = valid_pipeline[depth-1];
            acc     = acc_pipeline[depth][0];
        end
    endgenerate
endmodule

module fir_filter #(
    parameter int data_width = 16,
    parameter int weight_width = 16,
    parameter int taps = 31,
    parameter int parallel = 4,
    parameter logic signed [weight_width-1:0] weights[taps] = '{default: 0}
) (
    input  logic                         clk,
    input  logic                         rst,
    input  logic                         i_valid,
    input  logic signed [data_width-1:0] in,
    output logic                         o_valid,
    output logic signed [data_width-1:0] out
);
    /* verilator coverage_off */
    function automatic logic check_symmetrical();
        for (int i = 0; i != taps / 2; ++i) begin
            if (weights[i] != weights[taps-i-1]) begin
                return 0;
            end
        end
        return 1;
    endfunction
    /* verilator coverage_on */

    localparam logic is_symmetrical = check_symmetrical();
    localparam int actual_parallel = is_symmetrical ? parallel * 2 : parallel;
    localparam int step = (taps + actual_parallel - 1) / actual_parallel;
    localparam int mac_delay = is_symmetrical ? 3 : 2;

    logic [data_width-1:0] pipeline[taps];

    always_ff @(posedge clk) begin
        if (rst) begin
            pipeline <= '{default: 0};
        end else if (i_valid) begin
            // 使pipeline中元素顺序与下标顺序一致
            pipeline[taps-1] <= in;
            for (int i = 0; i != taps - 1; ++i) begin
                pipeline[i] <= pipeline[i+1];
            end
        end
    end

    localparam int cnt_width = $clog2(step);
    logic                 busy;
    logic                 cnt_reset;
    logic                 trigger;
    logic                 trigger_d;
    logic [cnt_width-1:0] cnt;
    counter #(
        .width(cnt_width)
    ) u_cnt (
        .clk     (clk),
        .rst     (cnt_reset),
        .enable  (busy),
        .count   (cnt),
        /* verilator lint_off PINCONNECTEMPTY */
        .overflow()
        /* verilator lint_on PINCONNECTEMPTY */
    );

    always_comb begin
        localparam int target_step = step - 1;
        trigger_d = cnt == target_step[cnt_width-1:0];
        cnt_reset = rst || trigger_d;
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            busy <= 0;
        end else if (busy == 0 || trigger_d) begin
            busy <= i_valid;
        end
        if (rst) begin
            trigger <= 0;
        end else begin
            trigger <= trigger_d;
        end
    end

    generate
        localparam mac_acc_width = data_width + weight_width + $clog2(taps / parallel);
        localparam int index_width = $clog2(taps);
        localparam int valid_taps = is_symmetrical ? (taps + 1) / 2 : taps;
        localparam int reverse_valid_taps = is_symmetrical ? taps / 2 : taps;
        logic signed [mac_acc_width-1:0] mac_acc               [ parallel];
        logic                            mac_acc_valid_pipeline[mac_delay];

        /* verilator coverage_off */
        function automatic logic signed [data_width-1:0] get_data(input logic [index_width-1:0] index, bit reverse);
            logic [index_width-1:0] actual_valid_taps = reverse ? reverse_valid_taps[index_width-1:0] : valid_taps[index_width-1:0];
            if (index >= actual_valid_taps) begin
                return 0;
            end else begin
                logic [index_width-1:0] actual_index = reverse ? taps[index_width-1:0] - 1 - index : index;
                return pipeline[actual_index];
            end
        endfunction

        typedef logic signed [data_width-1:0] weights_t[valid_taps];
        function automatic weights_t get_actual_weights();
            weights_t actual_weights = '{default: 0};
            if (is_symmetrical) begin
                for (int i = 0; i != valid_taps; ++i) begin
                    actual_weights[i] = weights[i];
                end
            end else begin
                for (int i = 0; i != taps; ++i) begin
                    actual_weights[i] = weights[taps-1-i];
                end
            end
            return actual_weights;
        endfunction
        localparam weights_t actual_weights = get_actual_weights();

        localparam weight_index_width = $clog2(valid_taps);
        function automatic logic signed [weight_width-1:0] get_weight(input logic [index_width-1:0] index);
            if (index >= valid_taps[index_width-1:0]) begin
                return 0;
            end else begin
                return actual_weights[index[weight_index_width-1:0]];
            end
        endfunction
        /* verilator coverage_on */

        for (genvar i = 0; i != parallel; ++i) begin : gen_mac
            if (is_symmetrical) begin : gen_pre_add_mac
                pre_add_mac_unit #(
                    .data_width(data_width),
                    .weight_width(weight_width),
                    .acc_width(mac_acc_width)
                ) u_mac (
                    .clk    (clk),
                    .rst    (rst),
                    .clear  (trigger),
                    .i_valid(busy),
                    .data1  (get_data(cnt * parallel[index_width-1:0] + i, 0)),
                    .data2  (get_data(cnt * parallel[index_width-1:0] + i, 1)),
                    .weight (get_weight(cnt * parallel[index_width-1:0] + i)),
                    .acc    (mac_acc[i])
                );
            end else begin : gen_mac_unit
                mac_unit #(
                    .data_width(data_width),
                    .weight_width(weight_width),
                    .acc_width(mac_acc_width)
                ) u_mac (
                    .clk    (clk),
                    .rst    (rst),
                    .clear  (trigger),
                    .i_valid(busy),
                    .data   (get_data(cnt * parallel[index_width-1:0] + i, 0)),
                    .weight (get_weight(cnt * parallel[index_width-1:0] + i)),
                    .acc    (mac_acc[i])
                );
            end
        end

        always_ff @(posedge clk) begin
            if (rst) begin
                mac_acc_valid_pipeline <= '{default: 0};
            end else begin
                mac_acc_valid_pipeline[0] <= trigger_d;
                for (int i = 0; i != mac_delay - 1; ++i) begin
                    mac_acc_valid_pipeline[i+1] <= mac_acc_valid_pipeline[i];
                end
            end
        end
    endgenerate

    localparam acc_width = mac_acc_width + $clog2(parallel);
    logic signed [acc_width-1:0] acc;
    logic                        add_tree_valid;
    add_tree #(
        .width(mac_acc_width),
        .n(parallel)
    ) u_add_tree (
        .clk    (clk),
        .rst    (rst),
        .i_valid(mac_acc_valid_pipeline[mac_delay-1]),
        .data   (mac_acc),
        .acc    (acc),
        .o_valid(add_tree_valid)
    );

    localparam int round_width = acc_width - (weight_width - 1);
    localparam int ext_width   = round_width - data_width;
    logic [round_width-1:0] round;
    logic [ data_width-1:0] value;
    logic [  ext_width-1:0] ext;
    logic [  ext_width-1:0] value_ext;
    logic [ data_width-1:0] out_d;

    always_comb begin : scale_round_clip
        localparam int lsb = weight_width - 1;
        round        = acc[acc_width-1:lsb] + acc[lsb-1];
        {ext, value} = round;
        value_ext    = {ext_width{value[data_width-1]}};
        out_d        = ext == value_ext ? value : {round[round_width-1], {data_width - 1{1'b1}}};
    end

    always_ff @(posedge clk) begin
        o_valid <= add_tree_valid;
        if (add_tree_valid) begin
            out <= out_d;
        end
    end
endmodule

/* verilator lint_on DECLFILENAME */
