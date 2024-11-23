# Runtime for stm32f4 platform

Creates runtime environment that allows running apps on STM32L4. One needs to build the solution, link resulting static library with the application and push to the board. Solution contains implementation of `printf` that sends data over UART to the terminal.

## Building

```
cmake --preset stm32l4
cmake --build --preset stm32l4
```

Libraries will be compiled and placed in the ``build/pack`` directory.
