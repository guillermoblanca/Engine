# Puts the "before" and "after" screenshots of the water side by side (../documentation/img/water/compare_<name>.jpg, JPEG so the repository stays small), each labelled.
#
#   powershell -File tools/water_compare.ps1 [-Before ../documentation/img/water/before] [-After ../documentation/img/water/after] [-Out ../documentation/img/water]
#
# Every file name that exists in both folders gets a comparison. The screenshots come from the editor (see ../documentation/02_tecnico/motor/GRAFO_TERRENO.md, section 11):
#   Editor.exe --hidden "--exec=generate temperate 5; camera lake; wait 4; screenshot <file>.png; quit"
param(
	[string]$Before = "../documentation/img/water/before",
	[string]$After = "../documentation/img/water/after",
	[string]$Out = "../documentation/img/water",
	[string]$BeforeLabel = "ANTES  (generador 2)",
	[string]$AfterLabel = "DESPUES  (generador 3: agua por nodos)"
)

Add-Type -AssemblyName System.Drawing

function Resolve-Full([string]$path) { return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $path)) }
$beforeDir = Resolve-Full $Before; $afterDir = Resolve-Full $After; $outDir = Resolve-Full $Out
$bar = 34
$names = Get-ChildItem $afterDir -Filter *.png | Where-Object { $_.Name -notlike '_*' -and (Test-Path (Join-Path $beforeDir $_.Name)) } | ForEach-Object { $_.Name }

foreach ($name in $names) {
	$a = [System.Drawing.Image]::FromFile((Join-Path $beforeDir $name))
	$b = [System.Drawing.Image]::FromFile((Join-Path $afterDir $name))
	$h = [Math]::Max($a.Height, $b.Height)
	$sheet = New-Object System.Drawing.Bitmap ($a.Width + $b.Width + 6), ($h + $bar)
	$g = [System.Drawing.Graphics]::FromImage($sheet)
	$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
	$g.DrawImage($a, 0, $bar, $a.Width, $a.Height)
	$g.DrawImage($b, $a.Width + 6, $bar, $b.Width, $b.Height)
	$font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold)
	$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
	$g.DrawString($BeforeLabel, $font, [System.Drawing.Brushes]::White, 10, 5)
	$g.DrawString($AfterLabel, $font, [System.Drawing.Brushes]::White, $a.Width + 16, 5)
	$target = Join-Path $outDir ("compare_" + [System.IO.Path]::GetFileNameWithoutExtension($name) + ".jpg")
	$jpeg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
	$quality = New-Object System.Drawing.Imaging.EncoderParameters 1
	$quality.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 88L
	$sheet.Save($target, $jpeg, $quality)
	$g.Dispose(); $sheet.Dispose(); $a.Dispose(); $b.Dispose()
	Write-Output $target
}
