module counter (
    input  logic       clk,
    input  logic       rst,
    output logic [3:0] count,
    output logic       overflow
);
    always_ff @(posedge clk) begin
        if (rst) begin
            count <= 0;
        end else begin
            {overflow, count} <= count + 1;
        end
    end
endmodule
