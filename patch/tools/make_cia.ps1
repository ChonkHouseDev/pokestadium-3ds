# make_cia.ps1: step 2. Builds build\pokestadium.cia from build\pokestadium.elf (made by the patch page from
# your own ROM) and whatever you put in user\. Empty folders use the defaults in cia_config\.
param([string]$UserDir = '')
$ErrorActionPreference = 'Stop'
$root  = Split-Path $PSScriptRoot -Parent
$user  = if ($UserDir) { (Resolve-Path $UserDir).Path } else { Join-Path $root 'user' }
$cfg   = Join-Path $root 'cia_config'
$build = Join-Path $root 'build'
$herr  = Join-Path $PSScriptRoot 'ext'
New-Item -ItemType Directory -Force -Path $herr, $build | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Drawing

function Bajar-Herramienta([string]$repo, [string]$exe, [string]$patron) {
    $ya = Get-ChildItem -Path $herr -Recurse -File -Filter $exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ya) { return $ya.FullName }
    Write-Host "Bajando $exe de github.com/$repo ..."
    try { $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'make_cia' } }
    catch { throw "No pude bajar $exe de github.com/$repo (ya no esta ahi). Bajalo a mano y pon $exe dentro de $herr" }
    $asset = $rel.assets | Where-Object { $_.name -imatch $patron } | Select-Object -First 1
    if (-not $asset) { $asset = $rel.assets | Where-Object { $_.name -imatch '\.zip$' } | Select-Object -First 1 }
    if (-not $asset) { throw "No encontre una descarga de $exe en $repo (version $($rel.tag_name)). Ponlo a mano en $herr" }
    $zip = Join-Path $herr $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
    $dest = Join-Path $herr ([IO.Path]::GetFileNameWithoutExtension($asset.name))
    Expand-Archive -Path $zip -DestinationPath $dest -Force
    Remove-Item $zip
    # en un zip con varias plataformas, la de Windows de 64 bits
    $todos = Get-ChildItem -Path $dest -Recurse -File -Filter $exe
    $f = $todos | Where-Object { $_.FullName -imatch 'win' -and $_.FullName -imatch '64' } | Select-Object -First 1
    if (-not $f) { $f = $todos | Select-Object -First 1 }
    if (-not $f) { throw "El zip de $repo no trae $exe. Ponlo a mano en $herr" }
    Write-Host "  $($f.FullName)"
    return $f.FullName
}


function First-Image([string]$dir) {
    if (-not (Test-Path $dir)) { return $null }
    Get-ChildItem -Path $dir -File | Where-Object { $_.Extension -imatch '^\.(png|jpg|jpeg|bmp)$' } | Sort-Object Name | Select-Object -First 1
}
function Fit([string]$src, [string]$dst, [int]$w, [int]$h) {
    $img = [System.Drawing.Image]::FromFile($src)
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = [Math]::Min($w / $img.Width, $h / $img.Height)
    $nw = [int][Math]::Round($img.Width * $s); $nh = [int][Math]::Round($img.Height * $s)
    $g.DrawImage($img, [int](($w - $nw) / 2), [int](($h - $nh) / 2), $nw, $nh)
    $g.Dispose(); $img.Dispose()
    $bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Trim-Wav([string]$src, [string]$dst) {
    $b = [IO.File]::ReadAllBytes($src); $enc = [Text.Encoding]::ASCII
    if ($b.Length -lt 44 -or $enc.GetString($b, 0, 4) -ne 'RIFF' -or $enc.GetString($b, 8, 4) -ne 'WAVE') { throw "$src is not a WAV file" }
    $pos = 12; $fmt = $null; $dataPos = -1; $dataLen = 0
    while ($pos + 8 -le $b.Length) {
        $id = $enc.GetString($b, $pos, 4); $len = [BitConverter]::ToInt32($b, $pos + 4)
        if ($id -eq 'fmt ') { $fmt = $pos + 8 }
        if ($id -eq 'data') { $dataPos = $pos + 8; $dataLen = [Math]::Min($len, $b.Length - $dataPos); break }
        $pos += 8 + $len + ($len % 2)
    }
    if ($null -eq $fmt -or $dataPos -lt 0) { throw "$src has no audio data" }
    if ([BitConverter]::ToInt16($b, $fmt) -ne 1 -or [BitConverter]::ToInt16($b, $fmt + 14) -ne 16) { throw "$src must be 16-bit PCM" }
    $rate = [BitConverter]::ToInt32($b, $fmt + 8); $align = [BitConverter]::ToInt16($b, $fmt + 12)
    $max = 3 * $rate; $max -= $max % $align
    $n = [Math]::Min($dataLen, $max); $n -= $n % $align
    $out = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter($out)
    $w.Write($enc.GetBytes('RIFF')); $w.Write([int](36 + $n)); $w.Write($enc.GetBytes('WAVEfmt ')); $w.Write([int]16)
    $w.Write($b, $fmt, 16); $w.Write($enc.GetBytes('data')); $w.Write([int]$n); $w.Write($b, $dataPos, $n); $w.Flush()
    [IO.File]::WriteAllBytes($dst, $out.ToArray())
}

$elf = Join-Path $build 'pokestadium.elf'
if (-not (Test-Path $elf)) { throw "Missing $elf (make it with the patch page from your own ROM)" }

# HOME menu: icon, banner, banner sound
$icon = Join-Path $build 'icon.png'
$f = First-Image (Join-Path $user 'icon')
if ($f) { Fit $f.FullName $icon 48 48; Write-Host "  icon: $($f.Name)" } else { Copy-Item (Join-Path $cfg 'icon.png') $icon -Force }
$cgfx = $null
if (Test-Path (Join-Path $user 'banner')) { $cgfx = Get-ChildItem -Path (Join-Path $user 'banner') -File -Filter *.cgfx | Select-Object -First 1 }
$bannerPng = Join-Path $build 'banner.png'
$f = First-Image (Join-Path $user 'banner')
if ($cgfx) { Write-Host "  banner (3D): $($cgfx.Name)" }
elseif ($f) { Fit $f.FullName $bannerPng 256 128; Write-Host "  banner: $($f.Name)" }
else { Copy-Item (Join-Path $cfg 'banner.png') $bannerPng -Force }
$wav = Join-Path $build 'banner.wav'
$s = $null
if (Test-Path (Join-Path $user 'banner_sound')) { $s = Get-ChildItem -Path (Join-Path $user 'banner_sound') -File -Filter *.wav | Select-Object -First 1 }
if ($s) { Trim-Wav $s.FullName $wav; Write-Host "  banner sound: $($s.Name)" } else { Copy-Item (Join-Path $cfg 'banner.wav') $wav -Force }

# In-game images (covers, minis, battle base) go in the RomFS as user.pscv; the game reads them on first boot
$entries = @()
$covers = Join-Path $user 'covers'
foreach ($pair in @(@('rojo', 'red'), @('azul', 'blue'), @('amarillo', 'yellow'))) {
    if (-not (Test-Path $covers)) { break }
    $c = Get-ChildItem -Path $covers -File | Where-Object { ($_.BaseName -ieq $pair[1] -or $_.BaseName -ieq $pair[0]) -and $_.Extension -imatch '^\.(png|jpg|jpeg|bmp)$' } | Select-Object -First 1
    if ($c) { $entries += @{ Name = $pair[0] + $c.Extension.ToLower(); File = $c.FullName }; Write-Host "  cover $($pair[1]): $($c.Name)" }
}
$names = 'bulbasaur,ivysaur,venusaur,charmander,charmeleon,charizard,squirtle,wartortle,blastoise,caterpie,metapod,butterfree,weedle,kakuna,beedrill,pidgey,pidgeotto,pidgeot,rattata,raticate,spearow,fearow,ekans,arbok,pikachu,raichu,sandshrew,sandslash,nidoran-f,nidorina,nidoqueen,nidoran-m,nidorino,nidoking,clefairy,clefable,vulpix,ninetales,jigglypuff,wigglytuff,zubat,golbat,oddish,gloom,vileplume,paras,parasect,venonat,venomoth,diglett,dugtrio,meowth,persian,psyduck,golduck,mankey,primeape,growlithe,arcanine,poliwag,poliwhirl,poliwrath,abra,kadabra,alakazam,machop,machoke,machamp,bellsprout,weepinbell,victreebel,tentacool,tentacruel,geodude,graveler,golem,ponyta,rapidash,slowpoke,slowbro,magnemite,magneton,farfetchd,doduo,dodrio,seel,dewgong,grimer,muk,shellder,cloyster,gastly,haunter,gengar,onix,drowzee,hypno,krabby,kingler,voltorb,electrode,exeggcute,exeggutor,cubone,marowak,hitmonlee,hitmonchan,lickitung,koffing,weezing,rhyhorn,rhydon,chansey,tangela,kangaskhan,horsea,seadra,goldeen,seaking,staryu,starmie,mr-mime,scyther,jynx,electabuzz,magmar,pinsir,tauros,magikarp,gyarados,lapras,ditto,eevee,vaporeon,jolteon,flareon,porygon,omanyte,omastar,kabuto,kabutops,aerodactyl,snorlax,articuno,zapdos,moltres,dratini,dragonair,dragonite,mewtwo,mew'.Split(',')
$minisDir = Join-Path $user 'minis'
$nMinis = 0
if (Test-Path $minisDir) {
    $png = @{}
    foreach ($m in (Get-ChildItem -Path $minisDir -Recurse -File -Filter *.png)) { $k = $m.BaseName.ToLower(); if (-not $png.ContainsKey($k)) { $png[$k] = $m.FullName } }
    for ($i = 0; $i -lt $names.Length; $i++) {
        $num = $i + 1; $p = $null
        foreach ($k in @($names[$i], ('{0}' -f $num), ('{0:d3}' -f $num))) { if ($png.ContainsKey($k)) { $p = $png[$k]; break } }
        if ($p) { $entries += @{ Name = ('mini_{0:d3}.png' -f $num); File = $p }; $nMinis++ }
    }
    foreach ($k in 'pikachu-yellow', '152') { if ($png.ContainsKey($k)) { $entries += @{ Name = 'mini_152.png'; File = $png[$k] }; break } }
    Write-Host "  minis: $nMinis of 151"
}
$baseDir = Join-Path $user 'battle_base'
if (Test-Path $baseDir) {
    foreach ($k in 'ball', 'ball_rival', 'ball_swap') {
        $b = Get-ChildItem -Path $baseDir -File -Filter "$k.png" | Select-Object -First 1
        if ($b) { $entries += @{ Name = "$k.png"; File = $b.FullName }; Write-Host "  battle base: $($b.Name)" }
    }
}
$romfs = Join-Path $build 'romfs'
New-Item -ItemType Directory -Force -Path $romfs | Out-Null
Remove-Item (Join-Path $romfs 'user.pscv') -ErrorAction SilentlyContinue
[IO.File]::WriteAllText((Join-Path $romfs 'readme.txt'), 'User images for Pokemon Stadium 3DS')
# Your own ROM inside the .cia (rom\baserom.z64), so nothing has to be copied to the SD
$romIn = Join-Path $root 'rom\baserom.z64'
$romOut = Join-Path $romfs 'baserom.z64'
if ((Test-Path $romIn) -and ((Get-Item $romIn).Length -eq 33554432)) {
    Copy-Item $romIn $romOut -Force
    Write-Host '  ROM: rom\baserom.z64 goes inside the .cia'
} else {
    Remove-Item $romOut -ErrorAction SilentlyContinue
    if (Test-Path $romIn) { Write-Host '  ROM: rom\baserom.z64 is not 32 MB, not included' } else { Write-Host '  ROM: none, the game will read it from the SD' }
}
if ($entries.Count -gt 255) { throw 'Too many images (max 255)' }
if ($UserDir) { $entries = @(); Write-Host '  custom build: in-game images are already inside the .elf' }
if ($entries.Count -gt 0) {
    $ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter($ms)
    $bw.Write([byte]$entries.Count)
    foreach ($e in $entries) {
        $nb = [Text.Encoding]::ASCII.GetBytes($e.Name); $d = [IO.File]::ReadAllBytes($e.File)
        $bw.Write([byte]$nb.Length); $bw.Write($nb); $bw.Write([uint32]$d.Length); $bw.Write($d)
    }
    $bw.Flush(); $pack = $ms.ToArray()
    $fs = [IO.File]::Create((Join-Path $romfs 'user.pscv'))
    $fs.Write($pack, 0, $pack.Length); $fs.Write([BitConverter]::GetBytes([uint32]$pack.Length), 0, 4); $fs.Write([Text.Encoding]::ASCII.GetBytes('PSCV'), 0, 4); $fs.Close()
    Write-Host ("  in-game images: {0} ({1} KB)" -f $entries.Count, [int]($pack.Length / 1KB))
}

# Title settings with this RomFS
$rsf = Join-Path $build 'pokestadium.rsf'
$txt = [IO.File]::ReadAllText((Join-Path $cfg 'pokestadium.rsf'))
$txt = [regex]::Replace($txt, '(?m)^RomFs:\s*$(\r?\n\s*#[^\r\n]*)?', "RomFs:`r`n  RootPath: " + ($romfs -replace '\\', '/'))
[IO.File]::WriteAllText($rsf, $txt)

$makerom = Bajar-Herramienta '3DSGuy/Project_CTR' 'makerom.exe' 'makerom.*win.*\.zip$'
$banner = Bajar-Herramienta 'Epicpkmn11/bannertool' 'bannertool.exe' 'win.*\.zip$'
Push-Location $build
try {
    & $banner makesmdh -s 'POKEMON STADIUM' -l 'POKEMON STADIUM' -p 'Pokethrasher' -i icon.png -o icon.icn
    if ($LASTEXITCODE -ne 0) { throw 'bannertool makesmdh failed' }
    if ($cgfx) { & $banner makebanner -ci $cgfx.FullName -a banner.wav -o banner.bnr } else { & $banner makebanner -i banner.png -a banner.wav -o banner.bnr }
    if ($LASTEXITCODE -ne 0) { throw 'bannertool makebanner failed' }
    & $makerom -f cia -o pokestadium.cia -target t -exefslogo -rsf pokestadium.rsf -elf pokestadium.elf -icon icon.icn -banner banner.bnr
    if ($LASTEXITCODE -ne 0) { throw 'makerom failed' }
    Remove-Item icon.icn, banner.bnr -ErrorAction SilentlyContinue
    Write-Host ''
    Write-Host ("Built: {0}\pokestadium.cia ({1} KB). Install it with FBI." -f $build, [math]::Round((Get-Item pokestadium.cia).Length / 1KB))
} finally { Pop-Location }
