# Device Learnings

- Active compiled firmware is `src/paperbadge_main.cpp`; old Arduino/M5Unified `src/main.cpp` remains reference only.
- `src/CMakeLists.txt` registers the ESP-IDF app plus HAL/comic/library/settings components.
- Badge PNG rendering depends on the widened PNGdec buffer from the previous recovery pass.
- Touch events can queue during slow e-paper refreshes; error and expensive-render paths should drain touch before/after navigation.
- `display::set_inverted()` must be paired with `touch::set_inverted()` and restored after final-frame rendering.
- GT911 long-press is not available in the current tap queue; header fallback is top-left Back and top-right Home.
- Firmware miniz path should stay conservative; large manga conversion belongs in host preprocessing.
- Normal firmware remains well below the 12 MB app limit after this sprint.
