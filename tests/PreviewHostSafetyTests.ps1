param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference='Stop'
$text=Get-Content -LiteralPath $Source -Raw
$forbidden=@('CreateMDIWindow','DefMDIChildProc','WM_MDIDESTROY','WM_MDIGETACTIVE','WM_MDIMAXIMIZE')
foreach($token in $forbidden) {
    if($text.Contains($token)) { throw "Unsafe preview host token remains: $token" }
}
$required=@('CreateWindowExA','WS_EX_NOPARENTNOTIFY','MdiClientSubclassProc','CodeTabSubclassProc','activated through safe overlay tab')
foreach($token in $required) {
    if(!$text.Contains($token)) { throw "Safe preview overlay contract is missing: $token" }
}
Write-Host 'Preview host stays outside the native MDI document model'
