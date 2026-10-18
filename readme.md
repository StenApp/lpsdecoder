# PS2 LPS Decoder and Extractor (fixed)

Extracts and converts PS2 LithTech LPS sound archives (used by No One Lives Forever PS2 and similar titles).

## What was fixed

The original code had several problems with folder handling:

1. **Wrong header size** – the ID string is 32 bytes, not 28.
2. **Broken folder tree traversal** – the priority / recursive path building was unreliable and often produced empty or garbage folder names.
3. **No usable folder assignment** – files with the same name (e.g. `FIRE.WAV`) that live in different folders (`ENGLISH`, `AMERICAN`, `PLAYER` …) were flattened or lost.

### New behaviour

- Reads the folder table correctly.
- Assigns every file to its owning folder via `fileStartIndex` + `files` count.
- Cleans folder names (printable ASCII only).
- Writes output as:

```
out/
  ENGLISH/
    BACKUP1.wav
    SEEBODY1.wav
    ...
  AMERICAN/
    BACKUP1.wav
    SEEBODY1.wav
    ...
  PLAYER/
    FIRE.wav
    PAIN01.wav
    ...
  DOORS/
  DOG/
  ...
```

- Duplicate names inside the **same** folder get a numeric suffix (`_1`, `_2`).
- Still uses VGMStream for the final ADPCM → WAV conversion.

## Usage

```bash
# Windows (with VGMStream next to the exe)
lpsdecoder.exe -f t06s01.lps

# Only extract .ss2 (no conversion)
lpsdecoder.exe -f t06s01.lps -e

# Linux
./lpsdecoder -f t06s01.lps -v /path/to/vgmstream-cli
```

## Requirements

- VGMStream (`vgmstream-cli` or the Windows `test.exe`) for WAV conversion.
- C++17 compiler (uses `std::filesystem`).

## Credits

- Original tool by haekb
- Format research / BMS script by aluigi (zenhax)
- Folder-handling rewrite 2026
