#pragma once

// Trigger a crash inside the library via a libc double-free().
// Call graph:
//   trigger_crash()    ← called by the test app
//     level1()
//       level2()
//         level3()     ← double free() here → glibc → SIGABRT (6)
//
// If use_stack_alloc is non-zero, level3() also allocates a 2 KB array on
// the stack before crashing.
void trigger_crash(int use_stack_alloc);
