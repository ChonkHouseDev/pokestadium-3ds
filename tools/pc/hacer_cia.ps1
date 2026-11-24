# hacer_cia.ps1: arma pokestadium.cia (instalable con FBI; sale en el menu HOME) en ESTA PC.
# Usa lo que esta en esta misma carpeta (cia\):
#   pokestadium.elf   el juego (viene con cada compilacion)
#   pokestadium.rsf   permisos y datos del titulo
#   icon.png          icono 48x48 del menu HOME          } los puedes cambiar por los tuyos
#   banner.png        imagen 256x128 de arriba al elegirlo } (mismo nombre y tamano)
#   banner.wav        sonido del banner (silencio, 1 s)
# Necesita makerom (3DSGuy/Project_CTR) y bannertool (Epicpkmn11/bannertool, copia del de Steveice10). Si no estan en .\herramientas, la primera
# vez los baja de sus paginas de GitHub. Si la descarga falla, pon tu makerom.exe y bannertool.exe en .\herramientas.
$ErrorActionPreference = 'Stop'
$aqui = $PSScriptRoot
$herr = Join-Path $aqui 'herramientas'
New-Item -ItemType Directory -Force -Path $herr | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Bajar-Herramienta([string]$repo, [string]$exe, [string]$patron) {
    $ya = Get-ChildItem -Path $herr -Recurse -File -Filter $exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ya) { return $ya.FullName }
    Write-Host "Bajando $exe de github.com/$repo ..."
    try { $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'hacer_cia' } }
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

# Tus imagenes: deja en esta carpeta mi_icono.(png|jpg|bmp) y/o mi_banner.(png|jpg|bmp) de cualquier tamano;
# aqui se ajustan (sin deformar, centradas) a icon.png 48x48 y banner.png 256x128, que es lo que pide el .cia.
Add-Type -AssemblyName System.Drawing
function Ajustar([string]$nombre, [string]$salida, [int]$w, [int]$h) {
    $src = Get-ChildItem -Path $aqui -File | Where-Object { $_.BaseName -ieq $nombre -and $_.Extension -imatch '^\.(png|jpg|jpeg|bmp)$' } | Select-Object -First 1
    if (-not $src) { return }
    $img = [System.Drawing.Image]::FromFile($src.FullName)
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = [Math]::Min($w / $img.Width, $h / $img.Height)
    $nw = [int][Math]::Round($img.Width * $s); $nh = [int][Math]::Round($img.Height * $s)
    $g.DrawImage($img, [int](($w - $nw) / 2), [int](($h - $nh) / 2), $nw, $nh)
    $g.Dispose(); $img.Dispose()
    $bmp.Save((Join-Path $aqui $salida), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  $($src.Name) -> $salida (${w}x${h})"
}
Ajustar 'mi_icono' 'icon.png' 48 48
Ajustar 'mi_banner' 'banner.png' 256 128

# Tu sonido del banner: deja mi_sonido.wav (PCM de 16 bits, de cualquier duracion); aqui se recorta a 3 segundos.
$snd = Join-Path $aqui 'mi_sonido.wav'
if (Test-Path $snd) {
    $b = [IO.File]::ReadAllBytes($snd)
    $enc = [Text.Encoding]::ASCII
    if ($b.Length -lt 44 -or $enc.GetString($b, 0, 4) -ne 'RIFF' -or $enc.GetString($b, 8, 4) -ne 'WAVE') { throw 'mi_sonido.wav no es un WAV' }
    $pos = 12; $fmt = $null; $dataPos = -1; $dataLen = 0
    while ($pos + 8 -le $b.Length) {
        $id = $enc.GetString($b, $pos, 4); $len = [BitConverter]::ToInt32($b, $pos + 4)
        if ($id -eq 'fmt ') { $fmt = $pos + 8 }
        if ($id -eq 'data') { $dataPos = $pos + 8; $dataLen = [Math]::Min($len, $b.Length - $dataPos); break }
        $pos += 8 + $len + ($len % 2)
    }
    if ($null -eq $fmt -or $dataPos -lt 0) { throw 'mi_sonido.wav: no encontre el audio' }
    $tag = [BitConverter]::ToInt16($b, $fmt); $bits = [BitConverter]::ToInt16($b, $fmt + 14)
    if ($tag -ne 1 -or $bits -ne 16) { throw 'mi_sonido.wav debe ser PCM de 16 bits (en Audacity: Exportar > WAV > Signed 16-bit PCM)' }
    $rate = [BitConverter]::ToInt32($b, $fmt + 8); $align = [BitConverter]::ToInt16($b, $fmt + 12)
    $max = 3 * $rate; $max -= $max % $align
    $n = [Math]::Min($dataLen, $max); $n -= $n % $align
    $out = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter($out)
    $w.Write($enc.GetBytes('RIFF')); $w.Write([int](36 + $n)); $w.Write($enc.GetBytes('WAVEfmt ')); $w.Write([int]16)
    $w.Write($b, $fmt, 16); $w.Write($enc.GetBytes('data')); $w.Write([int]$n); $w.Write($b, $dataPos, $n); $w.Flush()
    [IO.File]::WriteAllBytes((Join-Path $aqui 'banner.wav'), $out.ToArray())
    Write-Host ("  mi_sonido.wav -> banner.wav ({0:n1} s)" -f ($n / $rate))
}

foreach ($n in 'pokestadium.elf', 'pokestadium.rsf', 'icon.png', 'banner.png', 'banner.wav') {
    if (-not (Test-Path (Join-Path $aqui $n))) { throw "Falta $n en $aqui" }
}
$makerom = Bajar-Herramienta '3DSGuy/Project_CTR' 'makerom.exe' 'makerom.*win.*\.zip$'
$banner = Bajar-Herramienta 'Epicpkmn11/bannertool' 'bannertool.exe' 'win.*\.zip$'   # el repo original (Steveice10) ya no existe

Push-Location $aqui
try {
    & $banner makesmdh -s 'POKEMON STADIUM' -l 'POKEMON STADIUM' -p 'Pokethrasher' -i icon.png -o icon.icn
    if ($LASTEXITCODE -ne 0) { throw 'bannertool makesmdh fallo' }
    # banner 3D: si hay un .cgfx en esta carpeta (el primero; mi_banner.cgfx si existe) se usa en lugar de banner.png
    $cgfx = Get-ChildItem -Path $aqui -File -Filter *.cgfx | Sort-Object { $_.BaseName -ine 'mi_banner' }, Name | Select-Object -First 1
    if ($cgfx) {
        Write-Host "  banner 3D: $($cgfx.Name)"
        & $banner makebanner -ci $cgfx.FullName -a banner.wav -o banner.bnr
    } else {
        & $banner makebanner -i banner.png -a banner.wav -o banner.bnr
    }
    if ($LASTEXITCODE -ne 0) { throw 'bannertool makebanner fallo' }
    & $makerom -f cia -o pokestadium.cia -target t -exefslogo -rsf pokestadium.rsf -elf pokestadium.elf -icon icon.icn -banner banner.bnr
    if ($LASTEXITCODE -ne 0) { throw 'makerom fallo' }
    Remove-Item icon.icn, banner.bnr -ErrorAction SilentlyContinue
    $kb = [math]::Round((Get-Item pokestadium.cia).Length / 1KB)
    Write-Host ''
    Write-Host "Listo: $aqui\pokestadium.cia ($kb KB). Instalalo con FBI desde la SD o por red."
} finally { Pop-Location }
