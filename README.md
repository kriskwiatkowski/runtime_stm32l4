# Runtime for stm32l4 platform

Creates runtime environment that allows running apps on STM32L4. One needs to build the solution, link resulting static library with the application and push to the board. Solution contains implementation of `printf` that sends data over UART to the terminal.

## Building

```
cmake --preset stm32l4
cmake --build --preset stm32l4
```

Libraries will be compiled and placed in the ``build/pack`` directory.


## Test program

To run test program:
```
st-flash --reset write out/stm32l4/pack/bin/hello.bin 0x8000000
```