$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$plugin = Join-Path $root 'packages/plugin'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist '.staging/plugin'
$packageJson = Get-Content -LiteralPath (Join-Path $root 'package.json') -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $plugin 'manggo.plugin.json') -Raw | ConvertFrom-Json
if ($packageJson.version -ne $manifest.version) {
    throw 'package.json and manggo.plugin.json versions must match.'
}

Remove-Item -LiteralPath (Join-Path $dist '.staging') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $plugin 'manggo.plugin.json') -Destination $stage
Copy-Item -LiteralPath (Join-Path $plugin 'main.js') -Destination $stage
Copy-Item -LiteralPath (Join-Path $plugin 'README.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage

$name = "manggo-multiwindows-$($manifest.version).mplugin"
$zip = Join-Path $dist "$name.zip"
$output = Join-Path $dist $name
Remove-Item -LiteralPath $zip, $output -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Move-Item -LiteralPath $zip -Destination $output

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($output)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    foreach ($required in @('manggo.plugin.json', 'main.js', 'README.md', 'LICENSE')) {
        if ($entries -notcontains $required) {
            throw "Plugin package is missing root entry $required."
        }
    }
}
finally {
    $archive.Dispose()
}
Remove-Item -LiteralPath (Join-Path $dist '.staging') -Recurse -Force
Write-Output $output
