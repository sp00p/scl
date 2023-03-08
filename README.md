# scl
a high-level programming language that compiles to CHIP-8 assembly

# Update
Hi just a little update on the current state of this project. I've finally got graphics working with SDL2 and have most (if not all) opcodes implemented correctly. I still have yet to implement audio and user input but it's definitely a good start!

If you'd like to try this out yourself you're probably going to have to recompile the binary (compiled on mac will add proper archives later). The current tests that work are the chip8-test-suite, the IBM Logo, and the BC_test. To change the test used, edit the line in load_rom where it assigns the memory address 0x1FF to a number (only for chip8-test-suite). For now, numbers between 1-3 work as expected. Until user input is added, tests 4 and 5 will NOT work.

## Todo
- [ ] Expression Evaluation
- [ ] Basic Arithmetic
- [ ] Strings
- [ ] Input/Output Handling
- [ ] Physics
