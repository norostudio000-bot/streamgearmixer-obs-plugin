param([switch]$Install)
& (Join-Path $PSScriptRoot "build-timed-plugin.ps1") -Install:$Install
