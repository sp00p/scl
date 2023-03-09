# scl
a high-level programming language that compiles to CHIP-8 assembly

# Update
Hi just a little update on the current state of this project. I've finally got graphics working with SDL2 and have most (if not all) opcodes implemented correctly. I still have yet to implement audio and user input but it's definitely a good start!

If you'd like to try this out yourself you're probably going to have to recompile the binary (compiled on mac will add proper archives later). All test ROMS should load correctly although they will not be completely usable until user input is added. To run a test, simply use the following:

```
./scl -t 1-5
```

Note that tests 4 and 5 will not work until user input is implemented however 1-3 should work as expected. To load a ROM normally you can use the following:

```
./scl <rom>
```

Or, to view the memory on load:

```
./scl -p <rom>
```

Which will print the memory once the ROM is loaded. I may make this dynamically update itself in the future for debugging purposes.


## Todo
- [X] Basic CPU Functionality
- [X] Sound
- [X] Graphics
- [ ] User Input
- [ ] Expression Evaluation
- [ ] Basic Arithmetic
- [ ] Strings
- [ ] Input/Output Handling
- [ ] Physics
