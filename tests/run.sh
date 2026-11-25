#!/bin/sh
# Run from the repository root. The state fixture needs 32-bit guest pointers.
set -eu
python3 -B tests/test_damage_package.py
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang -std=c11 -Wall -Wextra -Werror tests/render_test.c -o "$work/render"
"$work/render"
clang -std=c11 -O2 -Wall -Wextra -Werror -pthread tests/snapshot_test.c -o "$work/snapshot"
"$work/snapshot"
if command -v wasm-ld >/dev/null 2>&1; then
    set -- wasm-ld
else
    rust_sysroot=$(rustc --print sysroot)
    host=$(rustc -vV | sed -n 's/^host: //p')
    set -- "$rust_sysroot/lib/rustlib/$host/bin/rust-lld" -flavor wasm
fi
for fixture in state render_entry; do
    clang --target=wasm32 -O1 -Wall -Wextra -Werror -ffreestanding \
        -c "tests/$fixture.c" -o "$work/$fixture.o"
    "$@" --no-entry --export=main "$work/$fixture.o" -o "$work/$fixture.wasm"
    node -e 'WebAssembly.instantiate(require("fs").readFileSync(process.argv[1])).then(({instance}) => {
        const line = instance.exports.main();
        if (line) throw Error(process.argv[1] + " failed at line " + line);
    })' "$work/$fixture.wasm"
done
