// 探测11001010序列
module sequence_detector (
    input  logic clk,
    input  logic enable,
    input  logic bit_stream,
    output logic result
);
    typedef enum logic [2:0] {
        idle,
        s1,
        s11,
        s110,
        s1100,
        s11001,
        s110010,
        s1100101
    } status_enum;

    status_enum status_reg_q;
    status_enum status_reg_d;
    // 在Verilator下状态机提取要求复位表达式为直接变量引用
    logic       rst;

    always_ff @(posedge clk) begin
        if (rst) begin
            status_reg_q <= idle;
        end else begin
            status_reg_q <= status_reg_d;
        end
    end

    always_comb begin
        rst = !enable;
        unique case (status_reg_q)
            idle: begin
                status_reg_d = bit_stream ? s1 : idle;
            end
            s1: begin
                status_reg_d = bit_stream ? s11 : idle;
            end
            s11: begin
                status_reg_d = bit_stream ? s11 : s110;
            end
            s110: begin
                status_reg_d = bit_stream ? s1 : s1100;
            end
            s1100: begin
                status_reg_d = bit_stream ? s11001 : idle;
            end
            s11001: begin
                status_reg_d = bit_stream ? s11 : s110010;
            end
            s110010: begin
                status_reg_d = bit_stream ? s1100101 : idle;
            end
            s1100101: begin
                status_reg_d = bit_stream ? s11 : idle;
            end
        endcase
    end

    // 使用三段式状态机以便让Verilator正确提取状态机逻辑
    always_ff @(posedge clk) begin
        result <= status_reg_q == s1100101 && !bit_stream;
    end
endmodule
