# ============================================================
#  mod-voicechat - voice.dll loader patch for Wow.exe 3.3.5a (12340)
#  SPDX-License-Identifier: GPL-2.0-or-later
# ============================================================
#
#  DE: Haengt eine eigene PE-Sektion ".vcl" an und biegt NUR den Einstiegspunkt
#      (AddressOfEntryPoint) darauf um. Der Stub ruft LoadLibraryA("voice.dll")
#      ueber die vorhandene Importtabelle auf und springt dann zum bisherigen
#      Einstiegspunkt weiter. Fehlt die DLL, startet WoW ganz normal.
#      Es wird kein einziges Byte in .text/.rdata/.data veraendert. Damit
#      kollidiert der Patch nicht mit Code-Caves, Detours oder anderen
#      angehaengten Sektionen (.hdp, .camr, AwesomeWotlk-Loader usw.).
#      Reihenfolge egal, mehrfach ausfuehren ist harmlos (erkennt ".vcl").
#
#  EN: Appends an own PE section ".vcl" and redirects ONLY the entry point
#      (AddressOfEntryPoint) to it. The stub calls LoadLibraryA("voice.dll")
#      through the existing import table and then jumps on to the previous
#      entry point. If the DLL is missing, WoW starts normally.
#      Not a single byte in .text/.rdata/.data is modified, so the patch does
#      not collide with code caves, detours or other appended sections.
#      Order does not matter, running it twice is harmless (detects ".vcl").
#
#  DE: Einbindung in apply_patches.ps1:   EN: Integration into apply_patches.ps1:
#      . "$PSScriptRoot\Add-VoiceLoader.ps1"
#      ,@('voice.dll laden (mod-voicechat)', { $script:f = Add-VoiceLoader -Image $script:f })
# ============================================================

function Add-VoiceLoader {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Image,
        [string]$DllName = 'voice.dll'
    )

    function VlU16([byte[]]$a, [int64]$o) { return [int64][BitConverter]::ToUInt16($a, $o) }
    function VlU32([byte[]]$a, [int64]$o) { return [int64][BitConverter]::ToUInt32($a, $o) }
    function VlAlign([int64]$v, [int64]$al) { $r = $v % $al; if ($r -eq 0) { return $v } return ($v + $al - $r) }
    function VlPut32([byte[]]$a, [int64]$o, [int64]$v) { if ($v -lt 0) { $v += 4294967296 }; [Array]::Copy([BitConverter]::GetBytes([uint32]$v), 0, $a, $o, 4) }
    function VlCStr([byte[]]$a, [int64]$o) {
        $end = $o; while ($a[$end] -ne 0) { $end++ }
        return [System.Text.Encoding]::ASCII.GetString($a, $o, $end - $o)
    }

    # --- PE-Header lesen / read PE header ---
    if ((VlU16 $Image 0) -ne 0x5A4D) { throw 'Add-VoiceLoader: keine gueltige EXE (MZ fehlt) / not a valid EXE' }
    $e = VlU32 $Image 0x3C
    if ((VlU32 $Image $e) -ne 0x4550) { throw 'Add-VoiceLoader: PE-Signatur fehlt / PE signature missing' }
    if ((VlU16 $Image ($e + 4)) -ne 0x14C) { throw 'Add-VoiceLoader: nur 32-Bit-EXE / 32-bit EXE only' }

    $nsec     = VlU16 $Image ($e + 6)
    $opt      = $e + 24
    $sectBase = $opt + (VlU16 $Image ($e + 20))
    $ep       = VlU32 $Image ($opt + 16)
    $ib       = VlU32 $Image ($opt + 28)
    $SA       = VlU32 $Image ($opt + 32)
    $FA       = VlU32 $Image ($opt + 36)

    # --- Sektionen einlesen, bereits gepatcht? / read sections, already patched? ---
    $sections = @()
    $maxEnd = 0
    $firstRaw = [int64]::MaxValue
    for ($i = 0; $i -lt $nsec; $i++) {
        $so   = $sectBase + 40 * $i
        $name = [System.Text.Encoding]::ASCII.GetString($Image, $so, 8).TrimEnd([char]0)
        $vs   = VlU32 $Image ($so + 8)
        $va   = VlU32 $Image ($so + 12)
        $rs   = VlU32 $Image ($so + 16)
        $rp   = VlU32 $Image ($so + 20)
        if ($name -eq '.vcl') {
            Write-Host '  [=] voice.dll-Loader ist bereits eingebaut / already installed'
            return , $Image
        }
        $sections += , @($va, [Math]::Max($vs, $rs), $rp)
        $end = $va + [Math]::Max($vs, $rs)
        if ($end -gt $maxEnd) { $maxEnd = $end }
        if ($rp -gt 0 -and $rp -lt $firstRaw) { $firstRaw = $rp }
    }

    function VlRvaToOff([int64]$rva) {
        foreach ($s in $sections) {
            if ($rva -ge $s[0] -and $rva -lt ($s[0] + $s[1])) { return $s[2] + ($rva - $s[0]) }
        }
        throw ('Add-VoiceLoader: RVA 0x{0:X} liegt in keiner Sektion / not inside any section' -f $rva)
    }

    # --- IAT-Eintrag von KERNEL32!LoadLibraryA suchen / find IAT slot of KERNEL32!LoadLibraryA ---
    $impRva = VlU32 $Image ($opt + 104)
    $iatVa = 0
    $d = VlRvaToOff $impRva
    while ((VlU32 $Image ($d + 12)) -ne 0 -and $iatVa -eq 0) {
        $dll = VlCStr $Image (VlRvaToOff (VlU32 $Image ($d + 12)))
        if ($dll -ieq 'kernel32.dll') {
            $oft = VlU32 $Image $d
            $ft  = VlU32 $Image ($d + 16)
            $lookup = if ($oft -ne 0) { $oft } else { $ft }
            $t = VlRvaToOff $lookup
            for ($k = 0; ; $k++) {
                $thunk = VlU32 $Image ($t + 4 * $k)
                if ($thunk -eq 0) { break }
                if (($thunk -band 0x80000000) -eq 0) {
                    if ((VlCStr $Image ((VlRvaToOff $thunk) + 2)) -ceq 'LoadLibraryA') {
                        $iatVa = $ib + $ft + 4 * $k
                        break
                    }
                }
            }
        }
        $d += 20
    }
    if ($iatVa -eq 0) { throw 'Add-VoiceLoader: KERNEL32!LoadLibraryA nicht importiert / not imported' }

    # --- Platz fuer einen weiteren Sektionseintrag? / room for another section header? ---
    $hoff = $sectBase + 40 * $nsec
    if (($hoff + 40) -gt $firstRaw -or ($hoff + 40) -gt (VlU32 $Image ($opt + 60))) {
        throw 'Add-VoiceLoader: kein Platz im PE-Header / no room in PE header'
    }
    for ($i = 0; $i -lt 40; $i++) {
        if ($Image[$hoff + $i] -ne 0) { throw 'Add-VoiceLoader: Header-Slot nicht leer / header slot not empty' }
    }

    # --- Neue Sektion bauen / build new section ---
    #   0x00  pushad                    60
    #   0x01  pushfd                    9C
    #   0x02  push  <dllname>           68 imm32
    #   0x07  call  [LoadLibraryA]      FF 15 imm32
    #   0x0D  popfd                     9D
    #   0x0E  popad                     61
    #   0x0F  jmp   <alter EP / old EP> E9 rel32
    #   0x14  int3 padding
    #   0x20  "voice.dll\0"
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($DllName)
    if ($nameBytes.Length -lt 1 -or $nameBytes.Length -gt 200) { throw 'Add-VoiceLoader: ungueltiger DLL-Name / invalid DLL name' }

    $newRva  = VlAlign $maxEnd $SA
    $vsize   = 0x20 + $nameBytes.Length + 1
    $rawSize = VlAlign $vsize $FA
    $newRaw  = VlAlign $Image.Length $FA

    $sec = New-Object byte[] $rawSize
    for ($i = 0; $i -lt 0x20; $i++) { $sec[$i] = 0xCC }
    $sec[0x00] = 0x60; $sec[0x01] = 0x9C
    $sec[0x02] = 0x68; VlPut32 $sec 0x03 ($ib + $newRva + 0x20)
    $sec[0x07] = 0xFF; $sec[0x08] = 0x15; VlPut32 $sec 0x09 $iatVa
    $sec[0x0D] = 0x9D; $sec[0x0E] = 0x61
    $sec[0x0F] = 0xE9; VlPut32 $sec 0x10 (($ep) - ($newRva + 0x14))
    [Array]::Copy($nameBytes, 0, $sec, 0x20, $nameBytes.Length)

    # --- Datei vergroessern und Sektion anhaengen / grow file and append section ---
    $nf = New-Object byte[] ($newRaw + $rawSize)
    [Array]::Copy($Image, 0, $nf, 0, $Image.Length)
    [Array]::Copy($sec, 0, $nf, $newRaw, $rawSize)

    # --- Header anpassen / update header ---
    [Array]::Copy([BitConverter]::GetBytes([uint16]($nsec + 1)), 0, $nf, ($e + 6), 2)
    VlPut32 $nf ($opt + 56) (VlAlign ($newRva + $vsize) $SA)   # SizeOfImage
    VlPut32 $nf ($opt + 16) $newRva                            # AddressOfEntryPoint
    $sh = New-Object byte[] 40
    [Array]::Copy([System.Text.Encoding]::ASCII.GetBytes('.vcl'), 0, $sh, 0, 4)
    VlPut32 $sh 8  $vsize
    VlPut32 $sh 12 $newRva
    VlPut32 $sh 16 $rawSize
    VlPut32 $sh 20 $newRaw
    VlPut32 $sh 36 0x60000020                                  # code | execute | read
    [Array]::Copy($sh, 0, $nf, $hoff, 40)

    Write-Host ('  [+] voice.dll-Loader: Sektion .vcl @ 0x{0:X}, EP 0x{1:X} -> 0x{2:X}, LoadLibraryA @ 0x{3:X}' -f ($ib + $newRva), ($ib + $ep), ($ib + $newRva), $iatVa)
    return , $nf
}
