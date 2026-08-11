$ErrorActionPreference = "Stop"

$script:Px4OverlayModules = @("uart_tracker", "track_control")
$script:Px4StartupCommands = @("uart_tracker start", "track_control start")
$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Get-SafeOverlayModuleTarget {
    param(
        [Parameter(Mandatory = $true)] [string]$Px4RootPath,
        [Parameter(Mandatory = $true)] [string]$ModuleName
    )

    if ($script:Px4OverlayModules -notcontains $ModuleName) {
        throw "Refusing unexpected overlay module: $ModuleName"
    }

    $modulesRoot = [IO.Path]::GetFullPath((Join-Path $Px4RootPath "src\modules")).TrimEnd('\', '/')
    $target = [IO.Path]::GetFullPath((Join-Path $modulesRoot $ModuleName)).TrimEnd('\', '/')
    $targetParent = [IO.Path]::GetDirectoryName($target).TrimEnd('\', '/')

    if (-not [string]::Equals($targetParent, $modulesRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe overlay module target: $target"
    }

    if (Test-Path -LiteralPath $target) {
        $item = Get-Item -Force -LiteralPath $target

        if (-not $item.PSIsContainer) {
            throw "Overlay module target is not a directory: $target"
        }

        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing reparse-point overlay target: $target"
        }
    }

    return $target
}

function Install-CleanOverlayModule {
    param(
        [Parameter(Mandatory = $true)] [string]$Px4RootPath,
        [Parameter(Mandatory = $true)] [string]$SourcePath,
        [Parameter(Mandatory = $true)] [string]$ModuleName
    )

    $source = [IO.Path]::GetFullPath($SourcePath).TrimEnd('\', '/')
    if (-not (Test-Path -PathType Container -LiteralPath $source)) {
        throw "Missing overlay module source: $source"
    }

    $target = Get-SafeOverlayModuleTarget -Px4RootPath $Px4RootPath -ModuleName $ModuleName

    if (Test-Path -LiteralPath $target) {
        Remove-Item -Recurse -Force -LiteralPath $target
        Write-Host "Removed stale module tree: $target"
    }

    Copy-Item -Recurse -Force -LiteralPath $source -Destination $target
    Write-Host "Installed module: $target"
    return $target
}

function ConvertTo-NormalizedBoardStartup {
    param([AllowEmptyString()] [string]$Content)

    $normalized = $Content.Replace("`r`n", "`n").Replace("`r", "`n")
    $lines = [Collections.Generic.List[string]]::new()

    if ($normalized.Length -gt 0) {
        $parts = $normalized.Split("`n")
        $partCount = $parts.Count

        if ($parts[-1] -eq "") {
            $partCount--
        }

        for ($index = 0; $index -lt $partCount; $index++) {
            $lines.Add($parts[$index])
        }
    }

    $firstCommandIndex = $null
    $remaining = [Collections.Generic.List[string]]::new()

    for ($index = 0; $index -lt $lines.Count; $index++) {
        $isStartupCommand = $script:Px4StartupCommands -contains $lines[$index].Trim()

        if ($isStartupCommand) {
            if ($null -eq $firstCommandIndex) {
                $firstCommandIndex = $index
            }
        } else {
            $remaining.Add($lines[$index])
        }
    }

    if ($null -eq $firstCommandIndex) {
        $insertAt = $remaining.Count

        if ($remaining.Count -gt 0 -and $remaining[-1] -ne "") {
            $remaining.Add("")
            $insertAt = $remaining.Count
        }
    } else {
        $insertAt = 0

        for ($index = 0; $index -lt $firstCommandIndex; $index++) {
            if ($script:Px4StartupCommands -notcontains $lines[$index].Trim()) {
                $insertAt++
            }
        }
    }

    for ($offset = 0; $offset -lt $script:Px4StartupCommands.Count; $offset++) {
        $remaining.Insert($insertAt + $offset, $script:Px4StartupCommands[$offset])
    }

    return (($remaining -join "`n").TrimEnd([char]10) + "`n")
}

function Assert-Px4BoardStartupContent {
    param([Parameter(Mandatory = $true)] [string]$Content)

    $lines = @($Content.Replace("`r`n", "`n").Replace("`r", "`n").Split("`n") | ForEach-Object { $_.Trim() })
    $indexes = @()

    foreach ($command in $script:Px4StartupCommands) {
        $matches = @($lines | Where-Object { $_ -eq $command })
        if ($matches.Count -ne 1) {
            throw "Board startup command must appear exactly once: $command"
        }
        $indexes += [Array]::IndexOf($lines, $command)
    }

    if ($indexes[0] -ge $indexes[1]) {
        throw "uart_tracker must start before track_control"
    }

    foreach ($line in $lines) {
        if ($line.Contains("&&") -and ($line.Contains("uart_tracker start") -or $line.Contains("track_control start"))) {
            throw "Board startup commands must not be joined with &&"
        }
    }
}

function Set-Px4BoardStartup {
    param([Parameter(Mandatory = $true)] [string]$Path)

    if (-not (Test-Path -PathType Leaf -LiteralPath $Path)) {
        throw "Missing rc.board_extras: $Path"
    }

    $content = [IO.File]::ReadAllText($Path)
    $normalized = ConvertTo-NormalizedBoardStartup -Content $content

    if ($normalized -cne $content) {
        [IO.File]::WriteAllText($Path, $normalized, $script:Utf8NoBom)
        Write-Host "Normalized tracker startup: $Path"
    }

    Assert-Px4BoardStartupContent -Content $normalized
}

function Assert-OverlayTreeMatch {
    param(
        [Parameter(Mandatory = $true)] [string]$SourcePath,
        [Parameter(Mandatory = $true)] [string]$TargetPath
    )

    $sourceRoot = [IO.Path]::GetFullPath($SourcePath).TrimEnd('\', '/')
    $targetRoot = [IO.Path]::GetFullPath($TargetPath).TrimEnd('\', '/')
    $sourceFiles = @{}
    $targetFiles = @{}

    foreach ($file in Get-ChildItem -Recurse -File -LiteralPath $sourceRoot) {
        $relative = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/').Replace('\', '/')
        $sourceFiles[$relative] = $file.FullName
    }

    foreach ($file in Get-ChildItem -Recurse -File -LiteralPath $targetRoot) {
        $relative = $file.FullName.Substring($targetRoot.Length).TrimStart('\', '/').Replace('\', '/')
        $targetFiles[$relative] = $file.FullName
    }

    $sourceNames = @($sourceFiles.Keys | Sort-Object)
    $targetNames = @($targetFiles.Keys | Sort-Object)
    if (Compare-Object $sourceNames $targetNames) {
        throw "Overlay file-set mismatch: $targetRoot"
    }

    foreach ($relative in $sourceNames) {
        $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFiles[$relative]).Hash
        $targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetFiles[$relative]).Hash
        if ($sourceHash -ne $targetHash) {
            throw "Overlay hash mismatch: $targetRoot/$relative"
        }
    }
}

function Assert-FileHashMatch {
    param(
        [Parameter(Mandatory = $true)] [string]$SourcePath,
        [Parameter(Mandatory = $true)] [string]$TargetPath
    )

    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $SourcePath).Hash
    $targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $TargetPath).Hash
    if ($sourceHash -ne $targetHash) {
        throw "Overlay file hash mismatch: $TargetPath"
    }
}
