param([string]$Dir)
Add-Type -AssemblyName System.Drawing
Get-ChildItem "$Dir\*.ppm","$Dir\*.pgm" | ForEach-Object {
  $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
  # parse header: magic\nW H\n255\n
  $pos = 0; $tokens = @()
  while ($tokens.Count -lt 4) {
    while ([char]$bytes[$pos] -match '\s') { $pos++ }
    $s = $pos; while (-not ([char]$bytes[$pos] -match '\s')) { $pos++ }
    $tokens += [System.Text.Encoding]::ASCII.GetString($bytes, $s, $pos - $s)
  }
  $pos++
  $w = [int]$tokens[1]; $h = [int]$tokens[2]; $gray = ($tokens[0] -eq 'P5')
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) {
    if ($gray) { $v = $bytes[$pos]; $pos++; $c = [System.Drawing.Color]::FromArgb(255, $v, $v, $v) }
    else { $c = [System.Drawing.Color]::FromArgb(255, $bytes[$pos], $bytes[$pos+1], $bytes[$pos+2]); $pos += 3 }
    $bmp.SetPixel($x, $y, $c)
  } }
  $out = [System.IO.Path]::ChangeExtension($_.FullName, ".png")
  $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
"ok"
