#!/bin/sh
# Run from the repository root. The state fixture needs 32-bit guest pointers.
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang -std=c11 -Wall -Wextra -Werror tests/render_test.c -o "$work/render"
"$work/render"
clang --target=wasm32 -O1 -Wall -Wextra -Werror -ffreestanding -c tests/state.c -o "$work/state.o"
if command -v wasm-ld >/dev/null 2>&1; then
    wasm-ld --no-entry --export=main "$work/state.o" -o "$work/state.wasm"
else
    rust_sysroot=$(rustc --print sysroot)
    host=$(rustc -vV | sed -n 's/^host: //p')
    "$rust_sysroot/lib/rustlib/$host/bin/rust-lld" -flavor wasm --no-entry --export=main \
        "$work/state.o" -o "$work/state.wasm"
fi
node -e 'WebAssembly.instantiate(require("fs").readFileSync(process.argv[1])).then(({instance}) => {
    const line = instance.exports.main();
    if (line) throw Error("state fixture failed at line " + line);
})' "$work/state.wasm"
