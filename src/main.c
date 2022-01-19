int dissassemble(unsigned char *buffer, int pc) {
    unsigned char *code = &buffer[pc];
    int opbytes = 1;
    printf("%04x", pc);
    switch(*code) {
        case 0x00: printf("NOP"); break;
        case 0x01: printf("LXI    B,#$%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x02: printf("STAX    B"); break;
        case 0x03: printf("INX    B"); break;
        case 0x04: printf("INR    B"); break;
        case 0x05: printf("DCR    B"); break;
        case 0x06: printf("MVI    B,#$%02x", code[1]); opbytes=2; break;
        case 0x07: printf("RCL"); break;
        case 0x08: printf("NOP"); break;
        case 0x09: printf("DAD    B"); break;
        case 0x0a: printf("LDAX    B"); break;
    }
}

int main (int argc, char** argv) {
    FILE *f = fopen(argv[1], "rb"); // open file provided with first argument
    if (f==NULL) { // if file is not found, error and exit
        printf("Error! Couldn't open file %s\n",  argv[1]);
        exit(1);
    }

    // get the size of the provided file and read to memory
    fseek(f, 0L, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0L, SEEK_SET);

    unsigned char *buffer = malloc(fsize);

    fread(buffer, fsize, l, f);
    fclose(f);

    int pc = 0;

    while (pc < fsize) {
        pc += dissassemble(buffer, pc);
    }
    return 0;
}
