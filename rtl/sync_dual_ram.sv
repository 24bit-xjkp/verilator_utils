typedef enum int {
    read_first,
    write_first,
    no_change
} sync_dual_ram_mode;

module sync_dual_ram #(
    parameter int data_width = 8,
    parameter int addr_width = 3,
    parameter sync_dual_ram_mode mode = no_change
) (
    input  logic                    clk,
    input  logic                    read_enable,
    input  logic [addr_width - 1:0] read_addr,
    output logic [data_width - 1:0] read_data,
    input  logic                    write_enable,
    input  logic [addr_width - 1:0] write_addr,
    input  logic [data_width - 1:0] write_data
);
    logic [data_width - 1:0] ram[1 << addr_width];

    generate
        case (mode)
            read_first: begin : gen_read_first
                always_ff @(posedge clk) begin
                    if (read_enable) begin
                        read_data <= ram[read_addr];
                    end
                    if (write_enable) begin
                        ram[write_addr] <= write_data;
                    end
                end
            end
            write_first: begin : gen_write_first
                logic                    read_write_simultaneously;
                logic [data_width - 1:0] read_data_ram;
                logic [data_width - 1:0] read_data_by_pass;
                always_ff @(posedge clk) begin
                    if (read_enable) begin
                        if (read_write_simultaneously) begin
                            read_data_by_pass <= write_data;
                        end else begin
                            read_data_ram <= ram[read_addr];
                        end
                    end
                    if (write_enable) begin
                        ram[write_addr] <= write_data;
                    end
                end
                always_comb begin
                    read_write_simultaneously = write_enable && read_addr == write_addr;
                    read_data                 = read_write_simultaneously ? read_data_by_pass : read_data_ram;
                end
            end
            no_change: begin : gen_no_change
                logic read_write_simultaneously;
                assign read_write_simultaneously = write_enable && read_addr == write_addr;
                always_ff @(posedge clk) begin
                    if (read_enable && !read_write_simultaneously) begin
                        read_data <= ram[read_addr];
                    end
                    if (write_enable) begin
                        ram[write_addr] <= write_data;
                    end
                end
            end
        endcase
    endgenerate
endmodule

/* verilator lint_off DECLFILENAME */
module sync_dual_ram_wrapper #(
    parameter int data_width = 8,
    parameter int addr_width = 3
) (
    input  logic                    clk,
    input  logic                    read_enable,
    input  logic [addr_width - 1:0] read_addr,
    output logic [data_width - 1:0] read_data_read_first,
    output logic [data_width - 1:0] read_data_write_first,
    output logic [data_width - 1:0] read_data_no_change,
    input  logic                    write_enable,
    input  logic [addr_width - 1:0] write_addr,
    input  logic [data_width - 1:0] write_data
);
    generate
        localparam sync_dual_ram_mode _ = read_first;
        localparam sync_dual_ram_mode modes[_.num()] = '{read_first, write_first, no_change};
        for (genvar i = 0; i != _.num(); i++) begin : gen_sync_dual_ram
            logic [data_width - 1:0] read_port;
            sync_dual_ram #(
                .data_width(data_width),
                .addr_width(addr_width),
                .mode(modes[i])
            ) u_sync_dual_ram (
                .clk         (clk),
                .read_enable (read_enable),
                .read_addr   (read_addr),
                .read_data   (read_port),
                .write_enable(write_enable),
                .write_addr  (write_addr),
                .write_data  (write_data)
            );

            always_comb begin
                unique case (modes[i])
                    read_first: begin
                        read_data_read_first = read_port;
                    end
                    write_first: begin
                        read_data_write_first = read_port;
                    end
                    no_change: begin
                        read_data_no_change = read_port;
                    end
                endcase
            end
        end
    endgenerate
endmodule
