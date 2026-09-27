# voice.dll loader for Wow.exe 3.3.5a

🇩🇪 [Deutsche Version](README.md)

A small exe patch that makes `Wow.exe` load `voice.dll` at startup. No injector or launcher needed.

## How it works

- The patch appends its own PE section **`.vcl`** (512 bytes) and redirects **only the entry point** in the PE header to it.
- The stub calls `LoadLibraryA("voice.dll")` through the existing import table, then jumps to the previous entry point.
- **If `voice.dll` is missing, WoW starts normally.**
- **Not a single byte** in `.text`, `.rdata` or `.data` is modified, so it does not collide with:
  - code caves in padding
  - detours
  - the AwesomeWotlk loader (`0x40B7D0` / `0x4E5CB0`)
  - other appended sections (`.hdp`, `.camr`)
- Order does not matter (before or after St0ny's patcher). Running it twice is harmless, because the patch recognises `.vcl`.

Tested on the original 12340 exe (SHA256 `AA63A575…8CB8`), once alone and once after all 34 active patches of St0ny's patcher. Only NumberOfSections, AddressOfEntryPoint, SizeOfImage, one new section header and the appended bytes change.

## Usage

**Standalone:** copy `Add-VoiceLoader.ps1`, `voice_patcher.ps1` and `voice_patcher.bat` into the WoW folder and run `voice_patcher.bat`. It creates `Wow.exe.voice.BAK` first.

```powershell
powershell -ExecutionPolicy Bypass -File voice_patcher.ps1 [-Path Wow.exe] [-DllName voice.dll]
```

**Built into St0ny's `apply_patches.ps1`:** put `Add-VoiceLoader.ps1` next to it and add:

```powershell
# at the top, after the helper functions:
. "$PSScriptRoot\Add-VoiceLoader.ps1"

# inside the $patches list:
,@('voice.dll laden (mod-voicechat)', {
    $script:f = Add-VoiceLoader -Image $script:f
})
```

## Note

Wow.exe is Blizzard's property and is **not** distributed with this project. The patcher only modifies your local copy.
