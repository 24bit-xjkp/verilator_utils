/* verilator lint_off DECLFILENAME */
`ifndef RTL_DUAL_RAM
`define RTL_DUAL_RAM

interface dual_ram_port #(
    parameter int data_width = 8,
    parameter int addr_width = 3
) (
    input logic clk
);
    logic                    enable;
    logic [addr_width - 1:0] addr;
    logic [data_width - 1:0] data;

    modport read(input clk, enable, addr, output data);
    modport write(input clk, enable, addr, data);
endinterface  // dual_ram_port

module dual_ram (
    dual_ram_port.read  read_port,
    dual_ram_port.write write_port
);
    localparam int data_width = read_port.data_width;
    localparam int addr_width = read_port.addr_width;

    logic [data_width - 1:0] ram[1 << addr_width];

    always_ff @(posedge read_port.clk) begin
        if (read_port.enable) begin
            read_port.data <= ram[read_port.addr];
        end
    end

    always_ff @(posedge write_port.clk) begin
        if (write_port.enable) begin
            ram[write_port.addr] <= write_port.data;
        end
    end
endmodule

`ifndef NO_TOP_MODULE_WRAPPER
module dual_ram_wrapper #(
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
    dual_ram_port #(
        .data_width(data_width),
        .addr_width(addr_width)
    ) read_port (
        .clk(read_clk)
    );
    dual_ram_port #(
        .data_width(data_width),
        .addr_width(addr_width)
    ) write_port (
        .clk(write_clk)
    );

    dual_ram u_dual_ram (
        .read_port (read_port.read),
        .write_port(write_port.write)
    );

    assign read_port.enable  = read_enable;
    assign read_port.addr    = read_addr;
    assign read_data         = read_port.data;
    assign write_port.enable = write_enable;
    assign write_port.addr   = write_addr;
    assign write_port.data   = write_data;
endmodule
`endif
`endif
