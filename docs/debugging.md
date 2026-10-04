# Debugging

Begin with a bounded run, a dedicated user directory, and final JSON state. Keep the exact input and command unchanged while locating the first divergence. A guest can remain in an exception handler until a run bound, so an exit code of zero needs its outcome and state interpreted together.

Use `--trace-after-boot` to avoid recording apploader execution when investigating game startup. `--trace-start` and `--trace-count` capture a narrow instruction window. Trace PCs and opcodes are the values actually fetched by the interpreter, including its instruction-cache state.

Storage-fault investigations should compare `srr0`, `dar`, `dsisr`, and the effective address formed by the faulting instruction. Then reproduce the relevant CPU-to-memory-to-device transaction in a small synthetic test. Testing only a device's canonical register address can miss a CPU-translated alias fault.

An unresolved physical data access can request the PI memory-access cause without a storage exception. A rejected page-table backing update instead increments `page_table_write_errors`, leaving its successful translation intact. Check those observations separately from DAR/DSISR and from invalid host-copy diagnostics; [memory boundaries](memory.md) explains their caller-specific address rules.

For a suspected loop, examine whether registers or counters change and capture its first entry. Sunshine's startup cache loop was initially bounded before its return; its prologue and decrementing CTR were correct. A complete run showed the large sweep finishing. Raising a bound is useful evidence when the trace demonstrates progress, but does not repair a fault or prove game compatibility.

Graphics failures should preserve the command stream before the first unsupported command or wrong pixel. Check command completeness, register masks, vertex layouts, transforms, clipping, TEV inputs, and pixel operations separately. Direct source comparisons use controlled inputs and preserve exact differences. Reference paths with undefined host operations need an explicit decision and a regression on defined inputs.

Run the focused regression first, then the relevant subsystem suite and original integration command. Keep the first failing trace and report what was run. Do not replace a failing result with success, suppress the diagnostic, or loosen a pixel comparison to get a passing test.
