/* verilator lint_off DECLFILENAME */

module sync_pipe #(  // 将自然码转格雷码并进行同步
    parameter int width
) (
    input  logic               clk,
    input  logic               rst,
    input  logic [width - 1:0] binary,      // 自然码
    output logic [width - 1:0] gray,        // 格雷码，组合输出
    output logic [width - 1:0] synced_gray  // 经clk同步的格雷码
);
    logic [width - 1:0] buffer;

    always_comb begin
        gray = (binary >> 1) ^ binary;
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            buffer      <= 0;
            synced_gray <= 0;
        end else begin
            buffer      <= gray;
            synced_gray <= buffer;
        end
    end
endmodule

module async_fifo #(
    parameter int data_width = 8,
    parameter int addr_width = 3
) (
    input  logic                    rst,
    input  logic                    i_clk,
    input  logic                    i_valid,
    output logic                    i_ready,
    input  logic [data_width - 1:0] i_data,
    input  logic                    o_clk,
    output logic                    o_valid,
    input  logic                    o_ready,
    output logic [data_width - 1:0] o_data
);
    logic                  read_enable;
    logic [addr_width-1:0] read_addr;
    logic [data_width-1:0] read_data;
    logic                  write_enable;
    logic [addr_width-1:0] write_addr;
    logic [data_width-1:0] write_data;
    logic                  o_valid_d;


    async_dual_ram #(
        .data_width(data_width),
        .addr_width(addr_width)
    ) u_async_dual_ram (
        .read_clk    (o_clk),
        .read_enable (read_enable),
        .read_addr   (read_addr),
        .read_data   (read_data),
        .write_clk   (i_clk),
        .write_enable(write_enable),
        .write_addr  (write_addr),
        .write_data  (write_data)
    );

    logic                full;
    logic                empty;
    logic [addr_width:0] i_cnt;
    logic [addr_width:0] o_cnt;
    /* verilator lint_off PINCONNECTEMPTY */
    counter #(
        .width(addr_width + 1)
    ) u_i_cnt (
        .clk     (i_clk),
        .rst     (rst),
        .enable  (write_enable),
        .count   (i_cnt),
        .overflow()
    );
    counter #(
        .width(addr_width + 1)
    ) u_o_cnt (
        .clk     (o_clk),
        .rst     (rst),
        .enable  (read_enable),
        .count   (o_cnt),
        .overflow()
    );
    /* verilator lint_on PINCONNECTEMPTY */

    logic [addr_width:0] i_gray_cnt;
    // 经过o_clk同步的i_gray_cnt
    logic [addr_width:0] i_synced_gray_cnt;
    logic [addr_width:0] o_gray_cnt;
    // 经过i_clk同步的o_gray_cnt
    logic [addr_width:0] o_synced_gray_cnt;
    sync_pipe #(
        .width(addr_width + 1)
    ) u_i_sync_pipe (
        .clk        (o_clk),
        .rst        (rst),
        .binary     (i_cnt),
        .gray       (i_gray_cnt),
        .synced_gray(i_synced_gray_cnt)
    );
    sync_pipe #(
        .width(addr_width + 1)
    ) u_o_sync_pipe (
        .clk        (i_clk),
        .rst        (rst),
        .binary     (o_cnt),
        .gray       (o_gray_cnt),
        .synced_gray(o_synced_gray_cnt)
    );

    always_comb begin
        localparam logic [addr_width:0] mask = 'b11 << (addr_width - 1);
        full         = i_gray_cnt == (o_synced_gray_cnt ^ mask);
        empty        = o_gray_cnt == i_synced_gray_cnt;

        write_data   = i_data;
        o_data       = read_data;
        i_ready      = !full;
        o_valid_d    = !empty;

        write_enable = i_ready && i_valid;
        read_enable  = o_ready && o_valid_d;

        write_addr   = i_cnt[addr_width-1:0];
        read_addr    = o_cnt[addr_width-1:0];
    end

    always_ff @(posedge o_clk) begin
        o_valid <= o_valid_d;
    end
endmodule
