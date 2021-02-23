$ProgressPreference = 'SilentlyContinue'

New-Item -Path "$PSScriptRoot\Asset" -Name "extra" -ItemType Directory -Force

Write-Output "Downloading rungholt"
Invoke-WebRequest -Uri "https://casual-effects.com/g3d/data10/research/model/rungholt/rungholt.zip" -OutFile "$PSScriptRoot\Asset\extra\rungholt.zip"
Write-Output "Unzipping rungholt"
powershell Expand-Archive "$PSScriptRoot\Asset\extra\rungholt.zip" -DestinationPath "$PSScriptRoot\Asset\extra\rungholt" -Force
Remove-Item "$PSScriptRoot\Asset\extra\rungholt.zip"
Write-Output "Done rungholt"