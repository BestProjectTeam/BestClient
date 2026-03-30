# Navigate to repo root
cd D:\GitHub\BestClient

# File content replacements
Write-Host "Renaming file contents..."

# Case-sensitive replacements
Get-ChildItem -Recurse -File -Include *.cpp,*.h,*.c,*.txt,*.md,*.cmake,*.json,*.xml | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    $newContent = $content `
        -replace 'TClient', 'BestClient' `
        -replace 'TCLIENT', 'BESTCLIENT' `
        -replace 'tclient', 'bestclient' `
        -replace 'T-Client', 'Best Client' `
        -replace 'TaterClient', 'BestClient'
    
    if ($content -ne $newContent) {
        Set-Content -Path $_.FullName -Value $newContent -NoNewline
        Write-Host "Updated: $($_.FullName)"
    }
}

# Rename files
Write-Host "Renaming files..."
Get-ChildItem -Recurse -File | Where-Object { $_.Name -match 'tclient|TClient' } | ForEach-Object {
    $newName = $_.Name -replace 'tclient', 'bestclient' -replace 'TClient', 'BestClient'
    Rename-Item -Path $_.FullName -NewName $newName
    Write-Host "Renamed file: $($_.Name) -> $newName"
}

# Rename directories
Write-Host "Renaming directories..."
Get-ChildItem -Recurse -Directory | Where-Object { $_.Name -match 'tclient|TClient' } | ForEach-Object {
    $newName = $_.Name -replace 'tclient', 'bestclient' -replace 'TClient', 'BestClient'
    Rename-Item -Path $_.FullName -NewName $newName
    Write-Host "Renamed dir: $($_.Name) -> $newName"
}

Write-Host "Rename complete!"
