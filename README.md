Small breakout implementation to be used within the [sphaero](https://sphaerophoria.dev/) simulation.

I didn't setup a proper compilation chain, as this is a single file project, but it can easily be integrated within the upstream sphaero codebase as a new chamber, or compiled standalone with clang using the `wasm32` toolchain (something like `clang --target=wasm32 -Wl,--no-entry,--export-all -nostdlib test.c -o test.wasm -L. -lphysics  -O3 -fno-builtin -mbulk-memory`).
