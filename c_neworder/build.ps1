$ErrorActionPreference = "Stop"

$include = "include"

$builds = @(
    @{
        out = "neworder_c.exe"
        src = @("src\main.c", "src\neworder.c", "src\sha256.c")
        desc = "smoke-test + benchmark"
    },
    @{
        out = "neworder_node.exe"
        src = @("src\node_main.c", "src\neworder.c", "src\nohttp.c", "src\sha256.c")
        desc = "HTTP node server"
    },
    @{
        out = "neworder_wallet.exe"
        src = @("src\wallet_main.c", "src\sha256.c")
        desc = "wallet CLI"
    }
)

function Find-Compiler {
    foreach ($cc in @("gcc", "clang")) {
        if (Get-Command $cc -ErrorAction SilentlyContinue) { return $cc }
    }
    if (Get-Command cl -ErrorAction SilentlyContinue) { return "cl" }
    return $null
}

$compiler = Find-Compiler
if (-not $compiler) {
    Write-Error "No C compiler found. Install gcc, clang, or Microsoft Visual C++ Build Tools."
    exit 1
}

foreach ($b in $builds) {
    Write-Host "Building $($b.out) [$($b.desc)]..."
    if ($compiler -eq "cl") {
        cl /O2 /W4 /I $include /Fe:$($b.out) $b.src
    } else {
        & $compiler -O2 -Wall -Wextra -std=c11 -I$include -o $b.out $b.src
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed for $($b.out)"
        exit $LASTEXITCODE
    }
    Write-Host "  -> $($b.out) OK"
}

Write-Host ""
Write-Host "All binaries built:"
foreach ($b in $builds) {
    Write-Host "  $($b.out)"
}
