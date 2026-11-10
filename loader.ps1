# One window. Three weight files. The first Run downloads llama.cpp b12180.
# Requires nothing beyond Windows itself.

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigDir = Join-Path $env:APPDATA "AnnoTuneLoader"
$Cache = Join-Path $ConfigDir "b12180"
$PathsFile = Join-Path $ConfigDir "paths.json"
$LogFile = Join-Path $ConfigDir "server.log"
$ErrFile = Join-Path $ConfigDir "server.err"
$Url = "http://127.0.0.1:8080"
$Release = "https://github.com/ggml-org/llama.cpp/releases/download/b12180"
$script:Proc = $null
$script:Owned = $false

function Test-Health {
    try {
        $response = Invoke-WebRequest -Uri "$Url/health" -UseBasicParsing -TimeoutSec 2
        return $response.Content -match '"status"\s*:\s*"ok"'
    } catch {
        return $false
    }
}

function Find-Server {
    $names = Get-ChildItem -LiteralPath $Root, (Join-Path $Root "bin"), $Cache -Filter "llama-server.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName
    if ($names) { return $names[0] }
    return $null
}

function Select-Assets {
    $arch = $env:PROCESSOR_ARCHITECTURE
    $gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name })
    $joined = $gpus -join " "
    if ($joined -match "NVIDIA") {
        if ($arch -eq "ARM64") {
            return @(
                "llama-b12180-bin-win-cuda-13.5-arm64.zip",
                "cudart-llama-bin-win-cuda-13.5-arm64.zip"
            )
        }
        return @(
            "llama-b12180-bin-win-cuda-12.4-x64.zip",
            "cudart-llama-bin-win-cuda-12.4-x64.zip"
        )
    }
    if ($arch -eq "ARM64") {
        if ($joined -match "Adreno") { return @("llama-b12180-bin-win-opencl-adreno-arm64.zip") }
        return @("llama-b12180-bin-win-vulkan-arm64.zip")
    }
    if ($joined -match "AMD|Radeon|Intel|Arc") {
        return @("llama-b12180-bin-win-vulkan-x64.zip")
    }
    return @("llama-b12180-bin-win-cpu-x64.zip")
}

function Get-ReleaseFile([string]$Name, [string]$Label) {
    $dest = Join-Path $Cache $Name
    if (Test-Path -LiteralPath $dest) { return $dest }
    $status.Text = "Downloading $Label. This happens once."
    $form.Refresh()
    $partial = "$dest.partial"
    $curl = Join-Path $env:SystemRoot "System32\curl.exe"
    & $curl.exe -L --fail --retry 3 --retry-delay 2 -o $partial "$Release/$Name"
    if ($LASTEXITCODE -ne 0) { throw "Could not download $Name" }
    Move-Item -LiteralPath $partial -Destination $dest -Force
    return $dest
}

function Install-Server {
    $found = Find-Server
    if ($found) { return $found }
    New-Item -ItemType Directory -Force -Path $Cache | Out-Null
    $assets = Select-Assets
    $label = "the model server"
    foreach ($name in $assets) {
        $archive = Get-ReleaseFile $name $label
        $status.Text = "Unpacking $name"
        $form.Refresh()
        & tar.exe -xf $archive -C $Cache
        if ($LASTEXITCODE -ne 0) { throw "Could not unpack $name" }
        $label = "the GPU libraries"
    }
    $found = Find-Server
    if (-not $found) { throw "The download did not contain llama-server.exe" }
    return $found
}

function Read-Saved {
    if (-not (Test-Path -LiteralPath $PathsFile)) { return @("", "", "") }
    try {
        $saved = Get-Content -LiteralPath $PathsFile -Raw | ConvertFrom-Json
        return @([string]$saved.base, [string]$saved.name, [string]$saved.ml)
    } catch {
        return @("", "", "")
    }
}

function Save-Paths([string]$Base, [string]$Name, [string]$Ml) {
    New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null
    @{ base = $Base; name = $Name; ml = $Ml } | ConvertTo-Json | Set-Content -LiteralPath $PathsFile -Encoding ASCII
}

function Start-Model([string]$Base, [string]$Name, [string]$Ml) {
    foreach ($path in @($Base, $Name, $Ml)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "File not found: $path"
        }
    }
    if (Test-Health) {
        throw "A model server is already running at $Url. This loader will not start another."
    }
    $binary = Install-Server
    New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null
    if (Test-Path -LiteralPath $LogFile) { Remove-Item -LiteralPath $LogFile -Force }
    if (Test-Path -LiteralPath $ErrFile) { Remove-Item -LiteralPath $ErrFile -Force }
    $binDir = Split-Path -Parent $binary
    $extra = @($binDir)
    if (Test-Path -LiteralPath $Cache) {
        $extra += Get-ChildItem -LiteralPath $Cache -Directory -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName }
    }
    $env:PATH = ($extra -join ";") + ";" + $env:PATH
    $script:Proc = Start-Process -FileName $binary -WorkingDirectory $binDir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $LogFile -RedirectStandardError $ErrFile -ArgumentList @(
        "-m", $Base,
        "--lora", "$Name,$Ml",
        "--lora-init-without-apply",
        "-c", "8192",
        "-ngl", "99",
        "-np", "1",
        "--no-cont-batching",
        "--no-cache-prompt",
        "--slot-prompt-similarity", "0",
        "--no-jinja",
        "--host", "127.0.0.1",
        "--port", "8080"
    )
    $script:Owned = $true
}

function Stop-Model {
    if (-not $script:Owned -or -not $script:Proc) { return }
    if (-not $script:Proc.HasExited) {
        & taskkill.exe /PID $script:Proc.Id /T /F 2>$null | Out-Null
    }
    $script:Owned = $false
    $script:Proc = $null
}

function Log-Tail {
    $lines = @()
    foreach ($path in @($ErrFile, $LogFile)) {
        if (Test-Path -LiteralPath $path) {
            $lines += Get-Content -LiteralPath $path -Tail 8 -ErrorAction SilentlyContinue
        }
    }
    return ($lines -join "`r`n")
}

$saved = Read-Saved
$form = New-Object System.Windows.Forms.Form
$form.Text = "AnnoTune Loader"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.StartPosition = "CenterScreen"
$form.ClientSize = New-Object System.Drawing.Size(560, 230)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

function Add-FileRow([string]$Caption, [int]$Top, [string]$Value) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $Caption
    $label.Location = New-Object System.Drawing.Point(16, $Top)
    $label.Size = New-Object System.Drawing.Size(110, 24)
    $box = New-Object System.Windows.Forms.TextBox
    $box.Text = $Value
    $box.Location = New-Object System.Drawing.Point(130, $Top)
    $box.Size = New-Object System.Drawing.Size(320, 24)
    $button = New-Object System.Windows.Forms.Button
    $button.Text = "Browse"
    $button.Location = New-Object System.Drawing.Point(458, ($Top - 2))
    $button.Size = New-Object System.Drawing.Size(84, 28)
    $button.Add_Click({
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = "GGUF (*.gguf)|*.gguf|All files (*.*)|*.*"
        $dialog.Title = $Caption
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            $box.Text = $dialog.FileName
        }
    }.GetNewClosure())
    $form.Controls.AddRange(@($label, $box, $button))
    return $box
}

$baseBox = Add-FileRow "Base model" 20 $saved[0]
$nameBox = Add-FileRow "Name adapter" 58 $saved[1]
$mlBox = Add-FileRow "ML adapter" 96 $saved[2]

$run = New-Object System.Windows.Forms.Button
$run.Text = "Run"
$run.Location = New-Object System.Drawing.Point(130, 142)
$run.Size = New-Object System.Drawing.Size(100, 32)
$stopButton = New-Object System.Windows.Forms.Button
$stopButton.Text = "Stop"
$stopButton.Location = New-Object System.Drawing.Point(240, 142)
$stopButton.Size = New-Object System.Drawing.Size(100, 32)
$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(16, 186)
$status.Size = New-Object System.Drawing.Size(528, 36)
$status.Text = "Choose the three weight files, then Run."
$form.Controls.AddRange(@($run, $stopButton, $status))

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$script:Waited = 0
$timer.Add_Tick({
    $script:Waited += 1
    if ($script:Proc -and $script:Proc.HasExited) {
        $timer.Stop()
        $status.Text = "The server stopped.`r`n" + (Log-Tail)
        $run.Enabled = $true
        return
    }
    if (Test-Health) {
        $timer.Stop()
        $status.Text = "Ready at $Url. Leave this window open while you annotate."
        return
    }
    if ($script:Waited -ge 180) {
        $timer.Stop()
        Stop-Model
        $status.Text = "The server did not become ready.`r`n" + (Log-Tail)
        $run.Enabled = $true
    }
})

$run.Add_Click({
    try {
        $run.Enabled = $false
        Start-Model $baseBox.Text.Trim() $nameBox.Text.Trim() $mlBox.Text.Trim()
        Save-Paths $baseBox.Text.Trim() $nameBox.Text.Trim() $mlBox.Text.Trim()
        $script:Waited = 0
        $status.Text = "Loading the model. The GPU build is selected for this computer."
        $timer.Start()
    } catch {
        $run.Enabled = $true
        $status.Text = $_.Exception.Message
    }
})

$stopButton.Add_Click({
    $timer.Stop()
    Stop-Model
    $run.Enabled = $true
    $status.Text = "Stopped."
})

$form.Add_FormClosing({ Stop-Model })
[void]$form.ShowDialog()
