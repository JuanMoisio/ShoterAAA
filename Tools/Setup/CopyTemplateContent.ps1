# Copies the Unreal Engine 5.8 template mannequin (meshes, animations) and the third-person rifle/pistol
# animation blueprints into Content/. They are Epic template content, so they are not stored in git:
# run this once after cloning the repository.
# Usage: powershell -ExecutionPolicy Bypass -File Tools\Setup\CopyTemplateContent.ps1
. "$PSScriptRoot\..\Build\ShoterCommon.ps1"

$engine = Find-UnrealEngine
$templates = Join-Path $engine 'Templates'

$characters = Join-Path $templates 'TemplateResources\High\Characters\Content'
robocopy $characters (Join-Path $ProjectRoot 'Content\Characters') /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "No se pudo copiar el maniquí desde $characters" }

$animTarget = Join-Path $ProjectRoot 'Content\Variant_Shooter\Anims'
New-Item -ItemType Directory -Force $animTarget | Out-Null
$animSource = Join-Path $templates 'TP_FirstPerson\Content\Variant_Shooter\Anims'
Copy-Item (Join-Path $animSource 'ABP_TP_Rifle.uasset'), (Join-Path $animSource 'ABP_TP_Pistol.uasset') $animTarget -Force

Write-Host "Contenido de plantilla copiado desde $engine" -ForegroundColor Green
