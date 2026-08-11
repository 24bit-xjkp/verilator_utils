module async_dual_ram #(
    parameter int data_width = 8,
    parameter int addr_width = 3
) (
    input  logic                      read_clk,
    input  logic                      read_enable,
    input  logic [addr_width - 1 : 0] read_addr,
    output logic [data_width - 1 : 0] read_data,
    input  logic                      write_clk,
    input  logic                      write_enable,
    input  logic [addr_width - 1 : 0] write_addr,
    input  logic [data_width - 1 : 0] write_data
);
    logic [data_width - 1:0] ram[1 << addr_width];

    always_ff @(posedge read_clk) begin
        if (read_enable) begin
            read_data <= ram[read_addr];
        end
    end

    always_ff @(posedge write_clk) begin
        if (write_enable) begin
            ram[write_addr] <= write_data;
        end
    end
endmodule
