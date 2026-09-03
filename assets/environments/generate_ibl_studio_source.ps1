param([Parameter(Mandatory = $true)][string]$Destination)

$ErrorActionPreference = 'Stop'

$width = 1024
$height = 512
$bytes = [System.Collections.Generic.List[byte]]::new(2200000)
$bytes.AddRange([System.Text.Encoding]::ASCII.GetBytes("#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y $height +X $width`n"))

function Add-LiteralChannel([System.Collections.Generic.List[byte]]$output, [byte[]]$channel) {
  for ($start = 0; $start -lt $channel.Length; $start += 128) {
    $count = [Math]::Min(128, $channel.Length - $start)
    $output.Add([byte]$count)
    for ($i = 0; $i -lt $count; ++$i) { $output.Add($channel[$start + $i]) }
  }
}

for ($y = 0; $y -lt $height; ++$y) {
  $bytes.Add(2); $bytes.Add(2); $bytes.Add([byte]($width -shr 8)); $bytes.Add([byte]($width -band 255))
  $channels = @([byte[]]::new($width), [byte[]]::new($width), [byte[]]::new($width), [byte[]]::new($width))
  $theta = [Math]::PI * ($y + 0.5) / $height
  $sinTheta = [Math]::Sin($theta)
  $dy = [Math]::Cos($theta)
  for ($x = 0; $x -lt $width; ++$x) {
    $phi = 2.0 * [Math]::PI * ($x + 0.5) / $width - [Math]::PI
    $dx = $sinTheta * [Math]::Cos($phi)
    $dz = $sinTheta * [Math]::Sin($phi)
    $floor = [Math]::Max(0.0, -$dy)
    $r = 0.035 + 0.055 * $floor
    $g = 0.045 + 0.045 * $floor
    $b = 0.065 + 0.025 * $floor
    $keyDot = $dx * -0.6047079 + $dy * 0.3527463 + $dz * 0.7054926
    $fillDot = $dx * 0.8616404 + $dy * 0.1230915 + $dz * 0.4923659
    $rimDot = $dx * 0.1616904 + $dy * 0.5659165 + $dz * -0.8084521
    $key = 9.0 * [Math]::Exp(($keyDot - 1.0) / 0.0022)
    $fill = 2.6 * [Math]::Exp(($fillDot - 1.0) / 0.0060)
    $rim = 4.0 * [Math]::Exp(($rimDot - 1.0) / 0.0035)
    $r += $key * 1.00 + $fill * 0.55 + $rim * 0.35
    $g += $key * 0.78 + $fill * 0.72 + $rim * 0.55
    $b += $key * 0.55 + $fill * 1.00 + $rim * 1.00
    $maximum = [Math]::Max($r, [Math]::Max($g, $b))
    $exponent = [Math]::Floor([Math]::Log($maximum, 2.0)) + 1
    $scale = [Math]::Pow(2.0, 8.0 - $exponent)
    $channels[0][$x] = [byte][Math]::Min(255, [Math]::Floor($r * $scale))
    $channels[1][$x] = [byte][Math]::Min(255, [Math]::Floor($g * $scale))
    $channels[2][$x] = [byte][Math]::Min(255, [Math]::Floor($b * $scale))
    $channels[3][$x] = [byte]($exponent + 128)
  }
  foreach ($channel in $channels) { Add-LiteralChannel $bytes $channel }
}

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
[System.IO.File]::WriteAllBytes($Destination, $bytes.ToArray())
