module edge_detector #(
    parameter int pipeline = 3
) (
    input  logic clk,
    input  logic rst,
    input  logic signal,
    output logic rising,
    output logic falling,
    output logic both
);
    logic [pipeline-1:0] shift_reg;
    logic                current;
    logic                previous;
    assign current  = shift_reg[pipeline-2];
    assign previous = shift_reg[pipeline-1];

    always_ff @(posedge clk) begin
        if (rst) begin
            shift_reg <= 0;
        end else begin
            shift_reg <= {shift_reg[pipeline-2:0], signal};
        end
    end

    always_comb begin
        rising  = current & ~previous;
        falling = ~current & previous;
        both    = current ^ previous;
    end
endmodule
