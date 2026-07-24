typedef enum logic {
    fibonacci,
    galois
} lfsr_feedback_t;

// 产生7位m序列的LFSR
module lfsr_m7 #(
    parameter lfsr_feedback_t feedback = galois
) (
    input  logic       clk,
    input  logic       rst,
    input  logic       enable,
    input  logic [6:0] initial_value,
    output logic       result
);
    logic [6:0] shift_register_q;
    logic [6:0] shift_register_d;
    logic       result_d;
    assign result_d = shift_register_q[0];

    always_ff @(posedge clk) begin
        if (rst) begin
            shift_register_q <= initial_value;
            result           <= 0;
        end else if (enable) begin
            shift_register_q <= shift_register_d;
            result           <= result_d;
        end
    end

    generate
        if (feedback == fibonacci) begin : gen_fibonacci_lfsr
            logic feedback_value;
            always_comb begin
                feedback_value   = shift_register_q[6] ^ shift_register_q[0];
                shift_register_d = {feedback_value, shift_register_q[6:1]};
            end
        end else if (feedback == galois) begin : gen_galois_lfsr
            logic [6:1] feedback_value;
            always_comb begin
                feedback_value = shift_register_q[6:1];
                feedback_value[6] ^= result_d;
                shift_register_d = {result_d, feedback_value};
            end
        end
    endgenerate
endmodule

/* verilator lint_off DECLFILENAME */
module lfsr_m7_wrapper (
    input  logic                 clk,
    input  logic                 rst,
    input  logic                 enable,
    input  logic           [6:0] initial_value,
    input  lfsr_feedback_t       lfsr_feedback,
    output logic                 result
);
    logic fibonacci_lfsr_enable;
    logic galois_lfsr_enable;
    logic fibonacci_lfsr_result;
    logic galois_lfsr_result;

    lfsr_m7 #(
        .feedback(fibonacci)
    ) u_fibonacci_lfsr (
        .clk          (clk),
        .rst          (rst),
        .enable       (fibonacci_lfsr_enable),
        .initial_value(initial_value),
        .result       (fibonacci_lfsr_result)
    );
    lfsr_m7 #(
        .feedback(galois)
    ) u_galois_lfsr (
        .clk          (clk),
        .rst          (rst),
        .enable       (galois_lfsr_enable),
        .initial_value(initial_value),
        .result       (galois_lfsr_result)
    );

    always_comb begin
        fibonacci_lfsr_enable = lfsr_feedback == fibonacci && enable;
        galois_lfsr_enable    = lfsr_feedback == galois && enable;
        result                = lfsr_feedback == fibonacci ? fibonacci_lfsr_result : galois_lfsr_result;
    end
endmodule
