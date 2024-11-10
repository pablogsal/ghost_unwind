#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdwarf/dwarf.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Structure to store frame information
typedef struct {
    unw_word_t ip;
    unw_word_t sp;
    unw_word_t ra_location;
    char func_name[256];
} frame_info_t;

static Dwarf_Debug dbg = NULL;

// Initialize DWARF debug info
static int init_dwarf(const char* executable) {
    int fd;
    Dwarf_Error error;
    
    fd = open(executable, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open executable: %s\n", strerror(errno));
        return -1;
    }
    
    if (dwarf_init(fd, DW_DLC_READ, NULL, NULL, &dbg, &error) != DW_DLV_OK) {
        fprintf(stderr, "Failed to initialize DWARF: %s\n", dwarf_errmsg(error));
        close(fd);
        return -1;
    }
    
    return 0;
}

// Find FDE (Frame Description Entry) for given PC
static Dwarf_Fde find_fde(Dwarf_Addr pc) {
    Dwarf_Error error;
    Dwarf_Arange *aranges;
    Dwarf_Signed arange_count;
    Dwarf_Fde fde = NULL;
    
    // Get all FDEs
    if (dwarf_get_fdes(dbg, &fde, NULL, &error) != DW_DLV_OK) {
        fprintf(stderr, "Error getting FDEs: %s\n", dwarf_errmsg(error));
        return NULL;
    }
    
    // Find FDE for current PC
    Dwarf_Addr low_pc, high_pc;
    if (dwarf_get_fde_range(fde, &low_pc, &high_pc, NULL, &error) != DW_DLV_OK) {
        fprintf(stderr, "Error getting FDE range: %s\n", dwarf_errmsg(error));
        return NULL;
    }
    
    if (pc >= low_pc && pc < high_pc) {
        return fde;
    }
    
    return NULL;
}

// Get return address location from DWARF CFI
static unw_word_t get_ra_location(unw_cursor_t *cursor, Dwarf_Addr pc) {
    Dwarf_Error error;
    Dwarf_Fde fde;
    Dwarf_Signed rule_count;
    Dwarf_Frame_Op *rules;
    unw_word_t ra_location = 0;
    
    fde = find_fde(pc);
    if (!fde) {
        fprintf(stderr, "Could not find FDE for PC 0x%lx\n", (unsigned long)pc);
        return 0;
    }
    
    // Get frame instructions
    if (dwarf_get_fde_instr_bytes(fde, &rules, &rule_count, &error) != DW_DLV_OK) {
        fprintf(stderr, "Error getting frame instructions: %s\n", dwarf_errmsg(error));
        return 0;
    }
    
    // Evaluate frame instructions to find RA rule
    Dwarf_Regtable3 regtable;
    if (dwarf_get_fde_info_for_all_regs3(fde, pc, &regtable, NULL, &error) != DW_DLV_OK) {
        fprintf(stderr, "Error evaluating frame instructions: %s\n", dwarf_errmsg(error));
        return 0;
    }
    
    // Get return address rule
    Dwarf_Small ra_reg = DW_FRAME_REG_INITIAL_VALUE;  // Architecture dependent
    Dwarf_Signed offset = 0;
    
    // Find return address register and offset
    for (int i = 0; i < rule_count; i++) {
        if (rules[i].fp_register == ra_reg) {
            offset = rules[i].fp_offset;
            break;
        }
    }
    
    // Calculate actual return address location
    unw_word_t cfa;
    unw_get_reg(cursor, UNW_REG_SP, &cfa);  // Use SP as CFA
    ra_location = cfa + offset;
    
    return ra_location;
}

void print_ra_locations(const char* executable) {
    unw_cursor_t cursor;
    unw_context_t context;
    frame_info_t frame;
    
    // Initialize DWARF debug info
    if (init_dwarf(executable) != 0) {
        return;
    }
    
    // Initialize unwinding
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    
    printf("Stack frame analysis:\n");
    printf("%-20s %-20s %-20s %-30s %-20s\n", 
           "Frame Level", "IP", "SP", "Return Address Location", "Function");
    printf("--------------------------------------------------------------------------------\n");
    
    int frame_count = 0;
    
    // Walk the stack
    while (unw_step(&cursor) > 0) {
        // Get IP and SP
        unw_get_reg(&cursor, UNW_REG_IP, &frame.ip);
        unw_get_reg(&cursor, UNW_REG_SP, &frame.sp);
        
        // Get function name
        if (unw_get_proc_name(&cursor, frame.func_name, sizeof(frame.func_name), NULL) != 0) {
            strcpy(frame.func_name, "<unknown>");
        }
        
        // Get return address location using DWARF info
        frame.ra_location = get_ra_location(&cursor, frame.ip);
        
        printf("%-20d 0x%-18lx 0x%-18lx 0x%-28lx %s\n",
               frame_count,
               (unsigned long)frame.ip,
               (unsigned long)frame.sp,
               (unsigned long)frame.ra_location,
               frame.func_name);
        
        frame_count++;
    }
    
    // Cleanup
    dwarf_finish(dbg, NULL);
}

// Test functions to create call stack
void func3(void) {
    print_ra_locations("/proc/self/exe");
}

void func2(void) {
    func3();
}

void func1(void) {
    func2();
}

int main() {
    printf("Starting stack analysis...\n\n");
    func1();
    return 0;
}