/***********************************************************************/
/***********************************************************************
 Pipeline Cache Simulator Solution
 ***********************************************************************/
/***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define MAX_CACHE_SIZE 10240
#define CACHE_MISS_DELAY 10 // 10 cycle cache miss penalty
#define MAX_STAGES 5

// init the simulator
void iplc_sim_init(int index, int blocksize, int assoc);

// Cache simulator functions
void iplc_sim_LRU_replace_on_miss(int index, int tag); // Mary
void iplc_sim_LRU_update_on_hit(int index, int assoc); 
int iplc_sim_trap_address(unsigned int address); // Tim

// Pipeline functions
unsigned int iplc_sim_parse_reg(char *reg_str);
void iplc_sim_parse_instruction(char *buffer);
void iplc_sim_push_pipeline_stage(); // Tim and Will
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg,
                                     int reg1, int reg2_or_constant);
void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address); // Will
void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address); // Will
void iplc_sim_process_pipeline_branch(int reg1, int reg2); // Will
void iplc_sim_process_pipeline_jump(); // Lorelei
void iplc_sim_process_pipeline_syscall(); // Lorelei
void iplc_sim_process_pipeline_nop(); // Lorelei

// Outout performance results
void iplc_sim_finalize();

typedef struct associativity
{
    int vb; /* valid bit */
    int tag;
} assoc_t;

typedef struct cache_line
{
    assoc_t *assoc;
    int     *replacement;
} cache_line_t;

cache_line_t *cache=NULL;
int cache_index=0;
int cache_blocksize=0;
int cache_blockoffsetbits = 0;
int cache_assoc=0;
long cache_miss=0;
long cache_access=0;
long cache_hit=0;

char instruction[16];
char reg1[16];
char reg2[16];
char offsetwithreg[16];
unsigned int data_address=0;
unsigned int instruction_address=0;
unsigned int pipeline_cycles=0;   // how many cycles did you pipeline consume
unsigned int instruction_count=0; // home many real instructions ran thru the pipeline
unsigned int branch_predict_taken=0;
unsigned int branch_count=0;
unsigned int correct_branch_predictions=0;

unsigned int debug=0;
unsigned int dump_pipeline=1;

enum instruction_type {NOP, RTYPE, LW, SW, BRANCH, JUMP, JAL, SYSCALL};

typedef struct rtype
{
    char instruction[16];
    int reg1;
    int reg2_or_constant;
    int dest_reg;
    
} rtype_t;

typedef struct load_word
{
    unsigned int data_address;
    int dest_reg;
    int base_reg;
    
} lw_t;

typedef struct store_word
{
    unsigned int data_address;
    int src_reg;
    int base_reg;
} sw_t;

typedef struct branch
{
    int reg1;
    int reg2;
    
} branch_t;


typedef struct jump
{
    char instruction[16];
    
} jump_t;

typedef struct pipeline
{
    enum instruction_type itype;
    unsigned int instruction_address;
    union
    {
        rtype_t   rtype;
        lw_t      lw;
        sw_t      sw;
        branch_t  branch;
        jump_t    jump;
    }
    stage;
    
} pipeline_t;

enum pipeline_stages {FETCH, DECODE, ALU, MEM, WRITEBACK};

pipeline_t pipeline[MAX_STAGES];

/************************************************************************************************/
/* Cache Functions ******************************************************************************/
/************************************************************************************************/
/*
 * Correctly configure the cache.
 */
void iplc_sim_init(int index, int blocksize, int assoc)
{
    int i=0, j=0;
    unsigned long cache_size = 0;
    cache_index = index;
    cache_blocksize = blocksize;
    cache_assoc = assoc;
    
    
    cache_blockoffsetbits =
    (int) rint((log( (double) (blocksize * 4) )/ log(2)));
    /* Note: rint function rounds the result up prior to casting */
    
    cache_size = assoc * ( 1 << index ) * ((32 * blocksize) + 33 - index - cache_blockoffsetbits);
    
    printf("Cache Configuration \n");
    printf("   Index: %d bits or %d lines \n", cache_index, (1<<cache_index) );
    printf("   BlockSize: %d \n", cache_blocksize );
    printf("   Associativity: %d \n", cache_assoc );
    printf("   BlockOffSetBits: %d \n", cache_blockoffsetbits );
    printf("   CacheSize: %lu \n", cache_size );
    
    if (cache_size > MAX_CACHE_SIZE ) {
        printf("Cache too big. Great than MAX SIZE of %d .... \n", MAX_CACHE_SIZE);
        exit(-1);
    }
    
    cache = (cache_line_t *) malloc((sizeof(cache_line_t) * 1<<index));
    
    for (i = 0; i < (1<<index); i++) {
        cache[i].assoc = (assoc_t *)malloc((sizeof(assoc_t) * assoc));
        cache[i].replacement = (int *)malloc((sizeof(int) * assoc));
        
        for (j = 0; j < assoc; j++) {
            cache[i].assoc[j].vb = 0;
            cache[i].assoc[j].tag = 0;
            cache[i].replacement[j] = j;
        }
    }
    
    // init the pipeline -- set all data to zero and instructions to NOP
    for (i = 0; i < MAX_STAGES; i++) {
        // itype is set to O which is NOP type instruction
        bzero(&(pipeline[i]), sizeof(pipeline_t));
    }
}

/*
 * iplc_sim_trap_address() determined this is not in our cache.  Put it there
 * and make sure that is now our Most Recently Used (MRU) entry.
 */
void iplc_sim_LRU_replace_on_miss(int index, int tag)
{
    int i=0;
    //j=0;
    
    /* percolate everything up first */
    for(i = 0; i < cache_assoc-1; i++){
        cache[index].assoc[i] = cache[index].assoc[i+1];
        cache[index].replacement[i] = cache[index].replacement[i+1];
    }
    
    //the last index is the MRU entry, so it's replaced
    cache[index].assoc[cache_assoc-1].tag = tag;
    cache[index].assoc[cache_assoc-1].vb = 1;
    cache[index].replacement[cache_assoc-1] = 0;
    
    cache_miss++;
    cache_access++;
}

/*
 * iplc_sim_trap_address() determined the entry is in our cache.  Update its
 * information in the cache.
 */
void iplc_sim_LRU_update_on_hit(int index, int assoc)
{
    int i=0, j=0;

    for (j = 0; j < cache_assoc; j++)
        if (cache[index].replacement[j] == assoc)
            break;
    
    /* percolate everything up */
    for (i = j+1; i < cache_assoc; i++) {
        cache[index].replacement[i-1] = cache[index].replacement[i];
    }
    
    cache[index].replacement[cache_assoc-1] = assoc;
    
    cache_hit++;
    cache_access++;
}

/*
 * Check if the address is in our cache.  Update our counter statistics 
 * for cache_access, cache_hit, etc.  If our configuration supports
 * associativity we may need to check through multiple entries for our
 * desired index.  In that case we will also need to call the LRU functions.
 */
int iplc_sim_trap_address(unsigned int address)
{
    int i=0, index=0;
    int tag=0;
    int hit=0;
    
    int mask = (1 << cache_index) -1;
    index = address >> cache_blockoffsetbits & mask;//get our index
    tag = address >> (cache_index + cache_blockoffsetbits);//get our tag
    
    for(i=0; i <cache_assoc; i++){
        if(cache[index].assoc[i].tag == tag){//hit
            hit=1;
            iplc_sim_LRU_update_on_hit(index, i);
            break;//hit so we good
        }
    }
    
    if(hit == 0){//missed
        iplc_sim_LRU_replace_on_miss(index, tag);
    }
    /* expects you to return 1 for hit, 0 for miss */
    return hit;
}

/*
 * Just output our summary statistics.
 */
void iplc_sim_finalize()
{
    /* Finish processing all instructions in the Pipeline */
    while (pipeline[FETCH].itype != NOP  ||
           pipeline[DECODE].itype != NOP ||
           pipeline[ALU].itype != NOP    ||
           pipeline[MEM].itype != NOP    ||
           pipeline[WRITEBACK].itype != NOP) {
        iplc_sim_push_pipeline_stage();
    }
    
    printf(" Cache Performance \n");
    printf("\t Number of Cache Accesses is %ld \n", cache_access);
    printf("\t Number of Cache Misses is %ld \n", cache_miss);
    printf("\t Number of Cache Hits is %ld \n", cache_hit);
    printf("\t Cache Miss Rate is %f \n\n", (double)cache_miss / (double)cache_access);
    printf("Pipeline Performance \n");
    printf("\t Total Cycles is %u \n", pipeline_cycles);
    printf("\t Total Instructions is %u \n", instruction_count);
    printf("\t Total Branch Instructions is %u \n", branch_count);
    printf("\t Total Correct Branch Predictions is %u \n", correct_branch_predictions);
    printf("\t CPI is %f \n\n", (double)pipeline_cycles / (double)instruction_count);
}

/************************************************************************************************/
/* Pipeline Functions ***************************************************************************/
/************************************************************************************************/

/*
 * Dump the current contents of our pipeline.
 */
void iplc_sim_dump_pipeline()
{
    int i;
    
    for (i = 0; i < MAX_STAGES; i++) {
        switch(i) {
            case FETCH:
                printf("(cyc: %u) FETCH:\t %d: 0x%x \t", pipeline_cycles, pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case DECODE:
                printf("DECODE:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case ALU:
                printf("ALU:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case MEM:
                printf("MEM:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case WRITEBACK:
                printf("WB:\t %d: 0x%x \n", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            default:
                printf("DUMP: Bad stage!\n" );
                exit(-1);
        }
    }
}

/*
 * Check if various stages of our pipeline require stalls, forwarding, etc.
 * Then push the contents of our various pipeline stages through the pipeline.
 */
void iplc_sim_push_pipeline_stage()
{
    int i;
    int data_hit=1;
    
    /* 1. Count WRITEBACK stage is "retired" -- This I'm giving you */
    if (pipeline[WRITEBACK].instruction_address) {
        instruction_count++;
        if (debug)
            printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n",
                   pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles);
    }
    
    /* 2. Check for BRANCH and correct/incorrect Branch Prediction */
     if (pipeline[DECODE].itype == BRANCH) {
        int branch_taken = 0; //initialize the branch_taken variable... 0 means no

        //We want to record that the branch was taken
        branch_count += 1; //unsigned int that keeps track of the number of times a branch is taken... (initalized line 70)


        // initialize the fetch and decode instruction addresse variables so we may compare later
        double fetch_instruction_address; //long variable names, but for clarity's sake so everyone knows what they are
        double decode_instruction_address;

        fetch_instruction_address = pipeline[FETCH].instruction_address; //store these addresses in our variables
        decode_instruction_address = pipeline[DECODE].instruction_address;

        //We need to compare the addresses to tell if the branch was aleady predicted/taken
        //if the two addresses are more than a word apart (4 bits), then we're going to assume the branch WAS taken

        double address_difference = (fetch_instruction_address - decode_instruction_address); //find the difference
        if (fabs(address_difference) > 4){ // the absolute value of the difference
            branch_taken = 1; //The branch was taken
        }
        else
            branch_taken = 0; //The branch was not taken

        //If the CPU predicted the branch, record it
        if (branch_taken == branch_predict_taken){ 
            correct_branch_predictions +=1; //tallying the total number of correct predictions
            //if this happnes, we're done here
        }

        else{
            //if the branch was not predicted
            int pipeline_size; //initialize and record a variable to capture the size of the pipeline
            pipeline_size = sizeof(pipeline_t);

            //Copy the data and push it to the next stage
            memcpy(&pipeline[WRITEBACK], &pipeline[MEM], pipeline_size); //copy pipeline_size bits from pipline[MEM] to pipleline[WRITEBACK]
            memcpy(&pipeline[MEM], &pipeline[ALU], pipeline_size); //copy from the ALU to MEM
            memcpy(&pipeline[ALU], &pipeline[DECODE], pipeline_size); //copy from DECODE to ALU

            //Now reset decode
            memset(&(pipeline[DECODE]), 0, pipeline_size);
            pipeline_cycles += 1; //another pipeline cylce in the books

            //Now just increase the instuction count by 1
            instruction_count += 1;
        } //end else statement (if the branch wasn't predicted)
    } //end STEP 2 (Check for BRANCH and correct/incorrect Branch Prediction)
    
    /* 3. Check for LW delays due to use in ALU stage and if data hit/miss
     *    add delay cycles if needed.
     */
    if (pipeline[MEM].itype == LW) {
        int inserted_nop = 0;
        //check for hit/miss
        int hit = iplc_sim_trap_address(pipeline[MEM].stage.lw.data_address);
        //if missed, add delay cycles
        if(hit == 0){
            inserted_nop = CACHE_MISS_DELAY;
            pipeline_cycles += inserted_nop;
        }

        //see if our alu is an rtype
        //if it is, we need to move some register stuff around
        if (pipeline[ALU].itype == RTYPE){
                memcpy(&pipeline[WRITEBACK], &pipeline[MEM], sizeof(pipeline_t));//copy our current register to the writeback
                memset(&pipeline[MEM],0,sizeof(pipeline_t));//clear the register
                pipeline_cycles += hit;//add a cycle if we've hit
                instruction_count++; //we did a thing, increment number of things done
        }
    }
    
    /* 4. Check for SW mem acess and data miss .. add delay cycles if needed */
    if (pipeline[MEM].itype == SW) {

        //We just have to see if we have a data hit, and if not, add delay cycles
        //we're going to assign a value to data_hit (1 for hit, 0 for miss)
        data_hit = iplc_sim_trap_address(pipeline[MEM].stage.sw.data_address);
        //use trap address to see if the address is in our cache

        if(data_hit == 0){ // if the address is not in our cache...
            //add the delay cycles
            pipeline_cycles = (pipeline_cycles + (CACHE_MISS_DELAY-1));
        } //end if statement (miss)
    }//end SW 
    
    /* 5. Increment pipe_cycles 1 cycle for normal processing */
    pipeline_cycles += 1;
    /* 6. push stages thru MEM->WB, ALU->MEM, DECODE->ALU, FETCH->ALU */
    int pipeline_size; //initialize and record a variable to capture the size of the pipeline
    pipeline_size = sizeof(pipeline_t); 

    for(i = MAX_STAGES-1; i != FETCH; i--){
        //We just need to push everything to the next stage
        //So we're gonna loop through the number of stages and copy the pipelines
        memcpy(&pipeline[i], &pipeline[i-1], pipeline_size);
        //memcpy just copies pipeline[i-1] into pipeline[i]
    }
    
    // 7. This is a give'me -- Reset the FETCH stage to NOP via bezero */
    bzero(&(pipeline[FETCH]), sizeof(pipeline_t));
}

/*
 * This function is fully implemented.  You should use this as a reference
 * for implementing the remaining instruction types.
 */
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg, int reg1, int reg2_or_constant)
{
    /* This is an example of what you need to do for the rest */
    iplc_sim_push_pipeline_stage();
    
    pipeline[FETCH].itype = RTYPE;
    pipeline[FETCH].instruction_address = instruction_address;
    
    strcpy(pipeline[FETCH].stage.rtype.instruction, instruction);
    pipeline[FETCH].stage.rtype.reg1 = reg1;
    pipeline[FETCH].stage.rtype.reg2_or_constant = reg2_or_constant;
    pipeline[FETCH].stage.rtype.dest_reg = dest_reg;
}

void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address)
{
   /* You must implement this function */
    iplc_sim_push_pipeline_stage();
    //This code just follows the template outlined for rtype
    pipeline[FETCH].itype = LW; //the instruction for the fetch stage is LW
    pipeline[FETCH].instruction_address = instruction_address; //set the instruction address

//The struct for load_word is defined here (line 87)
/*
    typedef struct load_word {
    unsigned int data_address;
    int dest_reg;
    int base_reg;
    } lw_t;
*/
    //need to set the values of the struct members within load_word (data_address, dest_reg base_reg)
    pipeline[FETCH].stage.lw.dest_reg = dest_reg; // store the destination register
    pipeline[FETCH].stage.lw.base_reg = base_reg; // store the base register
    pipeline[FETCH].stage.lw.data_address = data_address; //store data_address
}

void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address)
{
    /* You must implement this function */
    iplc_sim_push_pipeline_stage();

    pipeline[FETCH].itype = SW; //set the instruction type to sw
    pipeline[FETCH].instruction_address = instruction_address;

//The struct for store word has members data_address, src_reg, and base_reg (line 95)
/*
    typedef struct store_word
    {
    unsigned int data_address;
    int src_reg;
    int base_reg;
    } sw_t;
*/
    //insert the values into the right places
    pipeline[FETCH].stage.sw.src_reg = src_reg; //source register

    pipeline[FETCH].stage.sw.base_reg = base_reg; //base register
    pipeline[FETCH].stage.sw.data_address = data_address; //data address
}

void iplc_sim_process_pipeline_branch(int reg1, int reg2)
{
    /* You must implement this function */
    iplc_sim_push_pipeline_stage();

    pipeline[FETCH].itype = BRANCH; //the instruction type is branch
    pipeline[FETCH].instruction_address = instruction_address;
/*
    typedef struct branch
    {
    int reg1;
    int reg2;
    
    } branch_t;
*/
    //The only two member variables that need to be set are register 1 and register 2

    pipeline[FETCH].stage.branch.reg1 = reg1; // register 1
    pipeline[FETCH].stage.branch.reg2 = reg2; //register 2
}

void iplc_sim_process_pipeline_jump(char *instruction)
{
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = JUMP;
    pipeline[FETCH].instruction_address = instruction_address;
    strcpy( pipeline[FETCH].stage.jump.instruction, instruction );
}

void iplc_sim_process_pipeline_syscall()
{
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = SYSCALL;
    pipeline[FETCH].instruction_address = instruction_address;
}

void iplc_sim_process_pipeline_nop()
{
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = NOP;
    pipeline[FETCH].instruction_address = instruction_address;
    
}

/************************************************************************************************/
/* parse Function *******************************************************************************/
/************************************************************************************************/

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
unsigned int iplc_sim_parse_reg(char *reg_str)
{
    int i;
    // turn comma into \n
    if (reg_str[strlen(reg_str)-1] == ',')
        reg_str[strlen(reg_str)-1] = '\n';
    
    if (reg_str[0] != '$')
        return atoi(reg_str);
    else {
        // copy down over $ character than return atoi
        for (i = 0; i < strlen(reg_str); i++)
            reg_str[i] = reg_str[i+1];
        
        return atoi(reg_str);
    }
}

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
void iplc_sim_parse_instruction(char *buffer)
{
    int instruction_hit = 0;
    int i=0, j=0;
    int src_reg=0;
    int src_reg2=0;
    int dest_reg=0;
    char str_src_reg[16];
    char str_src_reg2[16];
    char str_dest_reg[16];
    char str_constant[16];
    
    if (sscanf(buffer, "%x %s", &instruction_address, instruction ) != 2) {
        printf("Malformed instruction \n");
        exit(-1);
    }
    
    instruction_hit = iplc_sim_trap_address( instruction_address );
    
    // if a MISS, then push current instruction thru pipeline
    if (!instruction_hit) {
        // need to subtract 1, since the stage is pushed once more for actual instruction processing
        // also need to allow for a branch miss prediction during the fetch cache miss time -- by
        // counting cycles this allows for these cycles to overlap and not doubly count.
        
        printf("INST MISS:\t Address 0x%x \n", instruction_address);
        
        for (i = pipeline_cycles, j = pipeline_cycles; i < j + CACHE_MISS_DELAY - 1; i++)
            iplc_sim_push_pipeline_stage();
    }
    else
        printf("INST HIT:\t Address 0x%x \n", instruction_address);
    
    // Parse the Instruction
    
    if (strncmp( instruction, "add", 3 ) == 0 ||
        strncmp( instruction, "sll", 3 ) == 0 ||
        strncmp( instruction, "ori", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_src_reg,
                   str_src_reg2 ) != 5) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address);
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = iplc_sim_parse_reg(str_src_reg);
        src_reg2 = iplc_sim_parse_reg(str_src_reg2);
        
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lui", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_constant ) != 4 ) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address );
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = -1;
        src_reg2 = -1;
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lw", 2 ) == 0 ||
             strncmp( instruction, "sw", 2 ) == 0  ) {
        if ( sscanf( buffer, "%x %s %s %s %x",
                    &instruction_address,
                    instruction,
                    reg1,
                    offsetwithreg,
                    &data_address ) != 5) {
            printf("Bad instruction: %s at address %x \n", instruction, instruction_address);
            exit(-1);
        }
        
        if (strncmp(instruction, "lw", 2 ) == 0) {
            
            dest_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_lw(dest_reg, -1, data_address);
        }
        if (strncmp( instruction, "sw", 2 ) == 0) {
            src_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_sw( src_reg, -1, data_address);
        }
    }
    else if (strncmp( instruction, "beq", 3 ) == 0) {
        // don't need to worry about getting regs -- just insert -1 values
        iplc_sim_process_pipeline_branch(-1, -1);
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        iplc_sim_process_pipeline_jump( instruction );
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        /*
         * Note: no need to worry about forwarding on the jump register
         * we'll let that one go.
         */
        iplc_sim_process_pipeline_jump(instruction);
    }
    else if ( strncmp( instruction, "syscall", 7 ) == 0) {
        iplc_sim_process_pipeline_syscall( );
    }
    else if ( strncmp( instruction, "nop", 3 ) == 0) {
        iplc_sim_process_pipeline_nop( );
    }
    else {
        printf("Do not know how to process instruction: %s at address %x \n",
               instruction, instruction_address );
        exit(-1);
    }
}

/************************************************************************************************/
/* MAIN Function ********************************************************************************/
/************************************************************************************************/

int main()
{
    char trace_file_name[1024];
    FILE *trace_file = NULL;
    char buffer[80];
    int index = 10;
    int blocksize = 1;
    int assoc = 1;
    
    printf("Please enter the tracefile: ");
    scanf("%s", trace_file_name);
    
    trace_file = fopen(trace_file_name, "r");
    
    if ( trace_file == NULL ) {
        printf("fopen failed for %s file\n", trace_file_name);
        exit(-1);
    }
    
    printf("Enter Cache Size (index), Blocksize and Level of Assoc \n");
    scanf( "%d %d %d", &index, &blocksize, &assoc );
    
    printf("Enter Branch Prediction: 0 (NOT taken), 1 (TAKEN): ");
    scanf("%d", &branch_predict_taken );
    
    iplc_sim_init(index, blocksize, assoc);
    
    while (fgets(buffer, 80, trace_file) != NULL) {
        iplc_sim_parse_instruction(buffer);
        if (dump_pipeline)
            iplc_sim_dump_pipeline();
    }
    
    iplc_sim_finalize();
    return 0;
}
