module sync_fifo #(
    parameter int data_width = 8,
    parameter int addr_width = 3
) (
    input  logic                  clk,
    input  logic                  rst,
    input  logic                  i_valid,
    output logic                  i_ready,
    input  logic [data_width-1:0] i_data,
    output logic                  o_valid,
    input  logic                  o_ready,
    output logic [data_width-1:0] o_data
);
    logic                  o_valid_d;
    logic [addr_width-1:0] read_addr;
    logic [  addr_width:0] read_cnt;
    logic [data_width-1:0] read_data;
    logic                  read_enable;
    logic                  empty;
    logic [addr_width-1:0] write_addr;
    logic [  addr_width:0] write_cnt;
    logic [data_width-1:0] write_data;
    logic                  write_enable;
    logic                  full;

    sync_dual_ram #(
        .data_width(data_width),
        .addr_width(addr_width),
        .mode(read_first)
    ) u_sync_dual_ram (
        .clk         (clk),
        .read_enable (read_enable),
        .read_addr   (read_addr),
        .read_data   (read_data),
        .write_enable(write_enable),
        .write_addr  (write_addr),
        .write_data  (write_data)
    );
    /* verilator lint_off PINCONNECTEMPTY */
    counter #(
        .width(addr_width + 1)
    ) u_read_cnt (
        .clk     (clk),
        .rst     (rst),
        .enable  (read_enable),
        .count   (read_cnt),
        .overflow()
    );
    counter #(
        .width(addr_width + 1)
    ) u_write_cnt (
        .clk     (clk),
        .rst     (rst),
        .enable  (write_enable),
        .count   (write_cnt),
        .overflow()
    );
    /* verilator lint_on PINCONNECTEMPTY */

    always_comb begin
        localparam logic [addr_width:0] mask = 1 << addr_width;

        read_addr    = read_cnt[addr_width-1:0];
        write_addr   = write_cnt[addr_width-1:0];
        write_data   = i_data;
        o_data       = read_data;

        empty        = read_cnt == write_cnt;
        full         = (read_cnt ^ mask) == write_cnt;
        i_ready      = !full;
        o_valid_d    = !empty;

        read_enable  = o_valid_d && o_ready;
        write_enable = i_valid && i_ready;
    end

    always_ff @(posedge clk) begin
        o_valid <= o_valid_d;
    end
endmodule
