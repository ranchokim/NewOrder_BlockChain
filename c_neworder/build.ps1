$ErrorActionPreference = "Stop"

$src = @("src\main.c", "src\neworder.c")
$include = "include"
$out = "neworder_c.exe"

if (Get-Command gcc -ErrorAction SilentlyContinue) {
  gcc -O2 -Wall -Wextra -std=c11 -I$include -o $out $src
  exit $LASTEXITCODE
}

if (Get-Command clang -ErrorAction SilentlyContinue) {
  clang -O2 -Wall -Wextra -std=c11 -I$include -o $out $src
  exit $LASTEXITCODE
}

if (Get-Command cl -ErrorAction SilentlyContinue) {
  cl /O2 /W4 /I $include /Fe:$out $src
  exit $LASTEXITCODE
}

Write-Error "No C compiler found. Install gcc, clang, or Microsoft Visual C++ Build Tools."
