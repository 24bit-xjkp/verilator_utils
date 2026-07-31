module counter #(
    parameter int width = 4
) (
    input  logic               clk,
    input  logic               rst,
    input  logic               enable,
    output logic [width - 1:0] count,
    output logic               overflow
);
    always_ff @(posedge clk) begin
        if (rst) begin
            count    <= 0;
            overflow <= 0;
        end else if (enable) begin
            {overflow, count} <= count + 1;
        end
    end
endmodule
