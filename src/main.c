#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "wires.h"
#include "arithmetic.h"
#include "memory.h"
#include "registers.h"
#include "ip_reg.h"
#include "compute.h"

// major opcodes
#define RETURN_STOP    0x0
#define REG_ARITHMETIC 0x1
#define REG_MOVQ       0x2
#define REG_MOVQ_MEM   0x3
#define CFLOW          0x4
#define IMM_ARITHMETIC 0x5
#define IMM_MOVQ       0x6
#define IMM_MOVQ_MEM   0x7
#define LEAQ2          0x8
#define LEAQ3          0x9
#define LEAQ6          0xA
#define LEAQ7          0xB
#define IMM_CBRANCH    0xF

// minor opcodes
#define STOP 0x0
#define RETURN 0x1
#define JMP 0xF
#define CALL 0xE

int main(int argc, char* argv[]) {
    // Check command line parameters.
    if (argc < 2)
        error("missing name of programfile to simulate");

    if (argc < 3)
        error("Missing starting address (in hex notation)");

    /*** SETUP ***/
    // We set up global state through variables that are preserved between cycles.

    // Program counter / Instruction Pointer
    ip_reg_p ip = ip_reg_create();

    // Register file:
    reg_p regs = regs_create();

    // Memory:
    // Shared memory for both instructions and data.
    mem_p mem = memory_create();
    memory_read_from_file(mem, argv[1]);

    int start;
    int scan_res = sscanf(argv[2],"%x", &start);
    if (scan_res != 1)
        error("Unable to interpret starting address");

    // We must setup argv from commandline before enabling tracefile
    // validation.
    if (argc > 4) { // one or more additional arguments specified.
        if (strcmp(argv[3],"--") != 0)
            error("3rd arg must be '--' if additional args are provided");
        // arguments beyond '--' are loaded into argv area in memory
        memory_load_argv(mem, argc - 4, argv + 4);
    } else
        memory_load_argv(mem, 0, NULL);

    // memory is now set up correctly, and we can enable tracefile
    // validation if a tracefile has been specified.
    if (argc == 4) { // tracefile specified, hook memories to it
        memory_tracefile(mem, argv[3]);
        regs_tracefile(regs, argv[3]);
        ip_reg_tracefile(ip, argv[3]);
    }
    ip_write(ip, from_int(start), true);

    // a stop signal for stopping the simulation.
    bool stop = false;

    // We need the instruction number to show how far we get
    int instruction_number = 0;

    while (!stop) { // SIMULATION BEGINS - for each cycle:

        /*** FETCH ***/
        val pc = ip_read(ip);
        ++instruction_number;
        printf("%d %lx\n", instruction_number, pc.val);

        // We're fetching 10 bytes in the form of 10 vals with one byte each
        // depending on the instruction we may not use all of them
        val inst_bytes[10];
        memory_read_into_buffer(mem, pc, inst_bytes, true);

        /*** DECODE ***/
        // read 4 bit segments of the instruction. The following 6 segments
        // are, if present, always located at the same position in the instruction
        val major_op = pick_bits(4,  4, inst_bytes[0]);
        val minor_op = pick_bits(0,  4, inst_bytes[0]);
        val reg_d = pick_bits(4, 4, inst_bytes[1]);
        val reg_s = pick_bits(0, 4, inst_bytes[1]);
        val reg_z = pick_bits(4, 4, inst_bytes[2]);
        val shamt = pick_bits(0, 4, inst_bytes[2]);

        // decode instruction type from major operation code
        bool is_return_or_stop = is(RETURN_STOP, major_op);
        bool is_reg_arithmetic = is(REG_ARITHMETIC, major_op);
        bool is_imm_arithmetic = is(IMM_ARITHMETIC, major_op);
        bool is_reg_movq = is(REG_MOVQ, major_op);
        bool is_imm_movq = is(IMM_MOVQ, major_op);
        bool is_reg_movq_mem = is(REG_MOVQ_MEM, major_op);
        bool is_imm_movq_mem = is(IMM_MOVQ_MEM, major_op);
        bool is_cflow = is(CFLOW, major_op); /* note that this signal does not include return - though logically it could */
        bool is_leaq2  = is(LEAQ2, major_op);
        bool is_leaq3  = is(LEAQ3, major_op);
        bool is_leaq6  = is(LEAQ6, major_op);
        bool is_leaq7  = is(LEAQ7, major_op);
        bool is_imm_cbranch = is(IMM_CBRANCH, major_op);

        // Right now, we can only execute instructions with a size of 2.
        // TODO 2021:
        // TODO DIDDLY DONE
        
        // computes the first result corresponding to if the size should be 2 eg 00xx + ???
        val result1 = or(use_if(reduce_or(neg(4,and(major_op, from_int (12)))), from_int(2)),
            use_if(is_leaq2,from_int(2)));
        // Sets size to 6 or 7 for respectively leq6 7
        val result2 = or(use_if(is_leaq6, from_int(6)), use_if(is_leaq7, from_int(7)));
        // Sets size to 3 if leaq 3 and to 6 if arithmetic
        val result3 = or(use_if(is_imm_arithmetic, from_int(6)), use_if(is_leaq3,from_int(3)));

        // chooses between result 1 or result 2
        val intermediate = or(result1, result2);
        // chooses between intermediate and if the size should be 6
        val intermediate2 = or(use_if(reduce_or(and(neg(4, and(major_op, from_int(8))),and(major_op, from_int(4)))), from_int(6)), intermediate);
        // computes the final result 
        val ins_size = or(intermediate2, result3);

        // broad categorization of the instruction
        bool is_leaq = is_leaq2 || is_leaq3 || is_leaq6 || is_leaq7;
        bool is_move = is_reg_movq || is_reg_movq_mem || is_imm_movq || is_imm_movq_mem;
        bool is_mem_access = is_reg_movq_mem || is_imm_movq_mem;
        bool is_call = is_cflow && is(CALL, minor_op);
        bool is_return = is_return_or_stop & is(RETURN, minor_op);
        bool is_stop = is_return_or_stop & is(STOP, minor_op);

        // picking the proper immediate positions within the instruction:
        bool imm_i_pos3 = is_leaq7;  /* all other at position 2 */
        bool imm_p_pos6 = is_imm_cbranch; /* all other at position 2 */

        // unimplemented control signals:
        //Hvis den er enten load, eller store, så er major op = 0011 for register, eller 0111 for int
        //Hvis minor upcode er 1001 så er det store, og 0001 for load, for reg,
        //Hvis minor upcode er 1101 så er det store, og 0101 for load, for int,
        bool is_load  = ((is_imm_movq_mem && is(0x5,minor_op))||(is_reg_movq_mem && is(0x1,minor_op)));          // TODO 2021: Detect when we're executing a load
        bool is_store = ((is_imm_movq_mem && is(0xd,minor_op))||(is_reg_movq_mem && is(0x9 ,minor_op)));          // TODO 2021: Detect when we're executing a store
        // hvis det control flow, eller branch, skal dette være positivt
        // Checker om minor_op er en af de reserverede værdier : (0010||0011||11xx)
        bool not_reserved = !(reduce_and(4,and(from_int(3), minor_op)) || is(0x3, minor_op) || is(0x2, minor_op));
        // ser om hvorvidt det er register eller immediate conditional, den miderste kan i toerien fjernes
        bool is_conditional = ((is_cflow && not_reserved) || is_imm_cbranch); // TODO 2021: Detect if we are executing a conditional flow change
        bool is_Iconditional = is_imm_cbranch;
        bool is_Rconditional = (is_cflow && not_reserved);
        // TODO 2021: Add additional control signals you may need below....
        // program counter

        // setting up operand fetch and register read and write for the datapath:
        bool use_imm = is_imm_movq | is_imm_arithmetic | is_imm_cbranch;
        val reg_read_dz = or(use_if(!is_leaq, reg_d), use_if(is_leaq, reg_z));
        // - other read port is always reg_s
        // - write is always to reg_d
        bool reg_wr_enable = is_reg_arithmetic || is_imm_arithmetic || is_leaq || is_load || is_reg_movq || is_imm_movq || is_call;

        // control signals for the compute section:
        // - pick result of compute section
        bool use_agen = is_leaq || is_move;
        bool is_arithmetic = is_imm_arithmetic | is_reg_arithmetic;
        bool use_multiplier = is_arithmetic && (is(MUL, minor_op) || is(IMUL, minor_op));
        bool use_shifter = is_arithmetic && (is(SAR, minor_op) || is(SAL, minor_op) || is(SHR, minor_op));
        bool use_direct = is_reg_movq || is_imm_movq;
        bool use_alu = (is_arithmetic || is_conditional) && !(use_shifter | use_multiplier);

        // - control for agen
        bool use_s = (is(1,pick_bits(0,1,minor_op)) && use_agen) || is_reg_arithmetic || is_cflow;
        bool use_z = is(1,pick_bits(1,1,minor_op)) && use_agen;
        bool use_d = (is(1,pick_bits(2,1,minor_op)) && use_agen) || is_imm_arithmetic || is_imm_cbranch;
        // - control for the ALU (too easy)
        val alu_ctrl = minor_op;
        // - control for the multiplier
        bool mul_is_signed = is(IMUL, minor_op);

        // - control for the shifter
        bool shft_is_signed = is(SAR, minor_op) | is(SAL, minor_op);
        bool shft_is_left   = is(SAL, minor_op);

        /*** EXECUTE ***/

        // Datapath:
        //
        // read immediates based on instruction type from the instruction buffer
        val imm_offset_2 = or(or(put_bits(0, 8, inst_bytes[2]), put_bits(8,8, inst_bytes[3])),
                              or(put_bits(16, 8, inst_bytes[4]), put_bits(24,8, inst_bytes[5])));
        val imm_offset_3 = or(or(put_bits(0, 8, inst_bytes[3]), put_bits(8,8, inst_bytes[4])),
                              or(put_bits(16, 8, inst_bytes[5]), put_bits(24,8, inst_bytes[6])));
        val imm_offset_6 = or(or(put_bits(0, 8, inst_bytes[6]), put_bits(8,8, inst_bytes[7])),
                              or(put_bits(16, 8, inst_bytes[8]), put_bits(24,8, inst_bytes[9])));

        val imm_i = or(use_if( !imm_i_pos3, imm_offset_2), use_if( imm_i_pos3, imm_offset_3));
        val imm_p = or(use_if( !imm_p_pos6, imm_offset_2), use_if( imm_p_pos6, imm_offset_6));

        val sext_imm_i = sign_extend(31, imm_i);
        val sext_imm_p = sign_extend(31, imm_p);

        // read registers
        val reg_out_a = reg_read(regs, reg_read_dz);
        val reg_out_b = reg_read(regs, reg_s);
        val op_b = or(use_if(use_imm, sext_imm_i), use_if(!use_imm, reg_out_b));

        // perform calculations
        val agen_result = address_generate(reg_out_a, reg_out_b, sext_imm_i,
                           shamt, use_z, use_s, use_d);
        val alu_result = alu_execute(alu_ctrl, reg_out_a, op_b);
        val mul_result = multiplier(mul_is_signed, reg_out_a, op_b);
        val shifter_result = shifter(shft_is_left, shft_is_signed, reg_out_a, op_b);
        val compute_result = or(use_if(use_agen, agen_result),
                    or(use_if(use_multiplier, mul_result),
                       or(use_if(use_shifter, shifter_result),
                          or(use_if(use_direct, op_b),
                         use_if(use_alu, alu_result)))));
        //printf("result,%lx\n", compute_result);
        // address of succeeding instruction in memory
        bool sa;
        val pc_incremented  = add(pc, ins_size);

        // determine the next position of the program counter
        // TODO 2021: Add any additional sources for the next PC (for call, ret, jmp and conditional branch)

        // ser hvorvidt pc skal inkrementeres normalt, eller skal hoppe, checker hvor vi conditionals er sande
        //jump
        bool is_jump = (is(CFLOW, major_op) && is(IMM_CBRANCH, minor_op));
        // register conditional
        bool true_Rconditional = is_Rconditional && comparator(minor_op,reg_d, reg_s);
        // intermediate conditional
        bool true_Iconditional = is_Iconditional && comparator(minor_op,reg_d, pick_bits(0,32,inst_bytes[2]));
        bool is_incriment = !(true_Rconditional || true_Iconditional || is_jump || is_return);
        // sætter statements sammen
        val pc_check1 = use_if(is_jump, pick_bits(0,32,inst_bytes[2]));
        val pc_check2 = or(use_if(true_Rconditional, pick_bits(0,32,inst_bytes[2])), use_if(true_Iconditional, pick_bits(0,32,inst_bytes[6])));
        val pc_next = or(or(pc_check1, pc_check2),use_if(is_incriment, pc_incremented));
        //printf("Iconditional : %lx ,%lx\n", reg_d.val ,pick_bits(0,32,inst_bytes[6]).val);
        //printf("Iconditional : %d ,%lx\n", true_Iconditional ,pick_bits(0,32,inst_bytes[6]).val);
        //printf("\n jump %d, incriment %d\n, return %d", is_jump, is_incriment, is_return);
        /*** MEMORY ***/
        // read from memory if needed
        val mem_out = memory_read(mem, agen_result, is_load);

        /*** WRITE ***/
        // choose result to write back to register
        // TODO 2021: Add any additional results which need to be muxed in for writing to the destination register
        bool use_compute_result = !is_load && (use_agen || use_multiplier || use_shifter || use_direct || use_alu);
        // case: memory
        // call -> hvad så?
        // dæk alle cases
        val datapath_result = or(use_if(use_compute_result, compute_result),
                                 use_if(is_load, mem_out));

        // write to register if needed
        //printf("result,%lx, is true?: %d\n", datapath_result.val , use_compute_result);
        reg_write(regs, reg_d, datapath_result, reg_wr_enable);

        // write to memory if needed
        memory_write(mem, agen_result, reg_out_a, is_store);

        // update program counter
        ip_write(ip, pc_next, true);

        // terminate when returning to zero
        if (is_stop || (pc_next.val == 0 && is_return)) stop = true;
    }
    memory_destroy(mem);
    regs_destroy(regs);

    printf("Done\n");
}
