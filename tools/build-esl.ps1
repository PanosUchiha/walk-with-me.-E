


param([string]$Out = "$PSScriptRoot\..\package\Wayfarer.esp")

$ErrorActionPreference = 'Stop'

$SLOTS          = 10
$OWN_INDEX      = 0x01          
$FIRST_OBJECT   = 0x800
$PACK_BASE      = ($OWN_INDEX -shl 24) -bor $FIRST_OBJECT      
$QUST_FORMID    = 0x01000805                          
$SANDBOX_FORMID = 0x0100080B
$TRAVEL_TEMPLATE = 0x00016FAA   
$PLAYER_REF     = 0x00000014
$QUEST_PRIORITY = 0x60          
$FORM_VERSION   = 44

function NewBuf { , (New-Object System.Collections.Generic.List[byte]) }
function AddBytes($buf, [byte[]]$b) { foreach ($x in $b) { [void]$buf.Add($x) } }
function AddU32($buf, [uint32]$v) { AddBytes $buf ([BitConverter]::GetBytes($v)) }
function AddU16($buf, [uint16]$v) { AddBytes $buf ([BitConverter]::GetBytes($v)) }
function AddSig($buf, [string]$s) { AddBytes $buf ([System.Text.Encoding]::ASCII.GetBytes($s)) }
function ZStr([string]$s) { [System.Text.Encoding]::ASCII.GetBytes($s) + [byte]0 }


function AddSub($buf, [string]$sig, [byte[]]$data) {
    AddSig $buf $sig
    AddU16 $buf ([uint16]$data.Length)
    if ($data.Length -gt 0) { AddBytes $buf $data }
}

function MakeRecord([string]$sig, [uint32]$formID, [uint32]$flags, [byte[]]$data) {
    $r = NewBuf
    AddSig  $r $sig
    AddU32  $r ([uint32]$data.Length)
    AddU32  $r $flags
    AddU32  $r $formID
    AddU32  $r 0                       
    AddU16  $r ([uint16]$FORM_VERSION)
    AddU16  $r 0                       
    AddBytes $r $data
    return $r.ToArray()
}

function MakeGroup([string]$label, [byte[]]$payload) {
    $g = NewBuf
    AddSig  $g 'GRUP'
    AddU32  $g ([uint32]($payload.Length + 24))
    AddSig  $g $label                  
    AddU32  $g 0                       
    AddU32  $g 0                       
    AddU32  $g 0                       
    AddBytes $g $payload
    return $g.ToArray()
}



function PackageID([int]$slot) { if ($slot -lt 5) { return $PACK_BASE + $slot }; return $PACK_BASE + $slot + 1 }
function FollowerAliasID([int]$slot) { if ($slot -lt 5) { return $slot }; return $slot + 5 }
function MarkerAliasID([int]$slot) { if ($slot -lt 5) { return $slot + 5 }; return $slot + 10 }
function MakePackage([int]$slot) {
    $d = NewBuf
    AddSub $d 'EDID' (ZStr "WayfarerFollowPackage$slot")
    
    
    AddSub $d 'PKDT' ([byte[]](0x00,0x20,0x00,0x00, 0x12,0x00,0x00,0xFF, 0x80,0x00,0x00,0x00))
    AddSub $d 'PSDT' ([byte[]](0xFF,0xFF,0x00,0xFF, 0xFF,0x00,0x00,0x00, 0x00,0x00,0x00,0x00))

    AddSub $d 'QNAM' ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    $pkcu = NewBuf
    AddU32 $pkcu 3                     
    AddU32 $pkcu ([uint32]$TRAVEL_TEMPLATE)
    AddU32 $pkcu 3                     
    AddSub $d 'PKCU' $pkcu.ToArray()

    
    
    
    AddSub $d 'ANAM' (ZStr 'Location')
    $pldt = NewBuf
    AddU32 $pldt 8
    AddU32 $pldt ([uint32](MarkerAliasID $slot))
    AddU32 $pldt 65                                    
    AddSub $d 'PLDT' $pldt.ToArray()

    AddSub $d 'ANAM' (ZStr 'Bool');     AddSub $d 'CNAM' ([byte[]](0x00))   
    AddSub $d 'ANAM' (ZStr 'Bool');     AddSub $d 'CNAM' ([byte[]](0x00))   

    foreach ($u in @(0x00,0x02,0x04)) { AddSub $d 'UNAM' ([byte[]]($u)) }
    AddSub $d 'XNAM' ([byte[]](0x05))

    foreach ($blk in @('POBA','POEA','POCA')) {
        AddSub $d $blk @()
        AddSub $d 'INAM' ([byte[]](0,0,0,0))
        AddSub $d 'PDTO' ([byte[]](0,0,0,0,0,0,0,0))
    }
    return MakeRecord 'PACK' ([uint32](PackageID $slot)) 0 $d.ToArray()
}

function MakeGatherPackage([int]$slot) {
    $d = NewBuf
    AddSub $d 'EDID' (ZStr "WayfarerGatherPackage$slot")
    
    
    AddSub $d 'PKDT' ([byte[]](0x00,0x20,0x00,0x00, 0x12,0x00,0x00,0xFF, 0x80,0x00,0x00,0x00))
    AddSub $d 'PSDT' ([byte[]](0xFF,0xFF,0x00,0xFF, 0xFF,0x00,0x00,0x00, 0x00,0x00,0x00,0x00))

    $condition=NewBuf;AddU32 $condition 0;AddBytes $condition ([BitConverter]::GetBytes([float]2));AddU32 $condition 74
    AddU32 $condition (0x01000830+$slot);AddU32 $condition 0;AddU32 $condition 0;AddU32 $condition 0;AddU32 $condition ([uint32]::MaxValue)
    AddSub $d 'CTDA' $condition.ToArray()
    AddSub $d 'QNAM'  ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    $pkcu = NewBuf
    AddU32 $pkcu 3                     
    AddU32 $pkcu ([uint32]$TRAVEL_TEMPLATE)
    AddU32 $pkcu 3                     
    AddSub $d 'PKCU' $pkcu.ToArray()

    
    
    
    AddSub $d 'ANAM' (ZStr 'Location')
    $pldt = NewBuf
    AddU32 $pldt 8
    AddU32 $pldt ([uint32](30+$slot))
    AddU32 $pldt 45                                    
    AddSub $d 'PLDT' $pldt.ToArray()

    AddSub $d 'ANAM' (ZStr 'Bool');     AddSub $d 'CNAM' ([byte[]](0x00))   
    AddSub $d 'ANAM' (ZStr 'Bool');     AddSub $d 'CNAM' ([byte[]](0x00))   

    foreach ($u in @(0x00,0x02,0x04)) { AddSub $d 'UNAM' ([byte[]]($u)) }
    AddSub $d 'XNAM' ([byte[]](0x05))

    foreach ($blk in @('POBA','POEA','POCA')) {
        AddSub $d $blk @()
        AddSub $d 'INAM' ([byte[]](0,0,0,0))
        AddSub $d 'PDTO' ([byte[]](0,0,0,0,0,0,0,0))
    }
    return MakeRecord 'PACK' ([uint32](0x01000870+$slot)) 0 $d.ToArray()
}


function MakeSandboxPackage([int]$slot, [bool]$social = $false, [bool]$pose = $false) {
    $d=NewBuf
    $name = if($pose) { "WayfarerPosePackage$slot" } elseif($social) { "WayfarerSocialPackage$slot" } else { "WayfarerSandboxPackage$slot" }
    AddSub $d 'EDID' (ZStr $name)
    AddSub $d 'PKDT' ([byte[]](0x00,0x20,0x00,0x00,0x12,0x00,0x00,0xFF,0,0,0,0))
    AddSub $d 'PSDT' ([byte[]](0xFF,0xFF,0,0xFF,0xFF,0,0,0,0,0,0,0))
    
    $condition=NewBuf
    AddU32 $condition 0
    AddBytes $condition ([BitConverter]::GetBytes([float]1))
    AddU32 $condition 74
    $gate = if($pose) { 0x01000860+$slot } elseif($social) { 0x01000830+$slot } else { $SANDBOX_FORMID }
    AddU32 $condition $gate
    AddU32 $condition 0
    AddU32 $condition 0
    AddU32 $condition 0
    AddU32 $condition ([uint32]::MaxValue)
    AddSub $d 'CTDA' $condition.ToArray()
    AddSub $d 'QNAM' ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    $inputs=NewBuf;AddU32 $inputs 12;AddU32 $inputs 0x1C254;AddU32 $inputs 10
    AddSub $d 'PKCU' $inputs.ToArray()
    AddSub $d 'ANAM' (ZStr 'Location')
    $location=NewBuf
    if($social -or $pose) { AddU32 $location 2;AddU32 $location 0;AddU32 $location 80 } 
    else { AddU32 $location 8;AddU32 $location ([uint32](MarkerAliasID $slot));AddU32 $location 200 }
    AddSub $d 'PLDT' $location.ToArray()
    
    
    $behaviors=if($pose) { @(0,0,0,0,0,0,0,0,0,0) } elseif($social) { @(0,0,0,1,0,0,0,0,0,0) } else { @(0,1,0,1,1,1,1,1,0,0) }
    foreach($value in $behaviors) {AddSub $d 'ANAM' (ZStr 'Bool');AddSub $d 'CNAM' ([byte[]]($value))}
    AddSub $d 'ANAM' (ZStr 'Float');AddSub $d 'CNAM' ([BitConverter]::GetBytes([float]50))
    foreach($uid in @(0,14,1,3,4,5,6,31,7,25,27,29)){AddSub $d 'UNAM' ([byte[]]($uid))}
    AddSub $d 'XNAM' ([byte[]](32))
    foreach($blk in @('POBA','POEA','POCA')) {AddSub $d $blk @();AddSub $d 'INAM' ([byte[]](0,0,0,0));AddSub $d 'PDTO' ([byte[]](0,0,0,0,0,0,0,0))}
    $id=if($pose) { 0x01000840+$slot } elseif($social) { 0x01000820+$slot } else { 0x01000810+$slot }
    return MakeRecord 'PACK' ([uint32]$id) 0 $d.ToArray()
}
function MakeSeatPackage([int]$slot) {
    $d=NewBuf;AddSub $d 'EDID' (ZStr "WayfarerSeatPackage$slot")
    AddSub $d 'PKDT' ([byte[]](0x00,0x20,0,0,0x12,0x00,0,0xFF,0,0,0,0))
    AddSub $d 'PSDT' ([byte[]](0xFF,0xFF,0,0xFF,0xFF,0,0,0,0,0,0,0))
    $condition=NewBuf;AddU32 $condition 0;AddBytes $condition ([BitConverter]::GetBytes([float]2));AddU32 $condition 74
    AddU32 $condition (0x01000860+$slot);AddU32 $condition 0;AddU32 $condition 0;AddU32 $condition 0;AddU32 $condition ([uint32]::MaxValue)
    AddSub $d 'CTDA' $condition.ToArray();AddSub $d 'QNAM' ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    $inputs=NewBuf;AddU32 $inputs 3;AddU32 $inputs 0xA9277;AddU32 $inputs 2;AddSub $d 'PKCU' $inputs.ToArray()
    
    AddSub $d 'ANAM' (ZStr 'SingleRef');$target=NewBuf;AddU32 $target 4;AddU32 $target (20+$slot);AddU32 $target 0;AddSub $d 'PTDA' $target.ToArray()
    AddSub $d 'ANAM' (ZStr 'Float');AddSub $d 'CNAM' ([BitConverter]::GetBytes([float]120))
    AddSub $d 'ANAM' (ZStr 'Bool');AddSub $d 'CNAM' ([byte[]](0))
    foreach($uid in @(16,3,4)){AddSub $d 'UNAM' ([byte[]]($uid))};AddSub $d 'XNAM' ([byte[]](17))
    foreach($blk in @('POBA','POEA','POCA')) {AddSub $d $blk @();AddSub $d 'INAM' ([byte[]](0,0,0,0));AddSub $d 'PDTO' ([byte[]](0,0,0,0,0,0,0,0))}
    return MakeRecord 'PACK' ([uint32](0x01000850+$slot)) 0 $d.ToArray()
}
function MakeSandboxGlobal {
    $d=NewBuf;AddSub $d 'EDID' (ZStr 'WayfarerSandboxActive');AddSub $d 'FNAM' ([byte[]](0x66));AddSub $d 'FLTV' ([BitConverter]::GetBytes([float]0))
    return MakeRecord 'GLOB' ([uint32]$SANDBOX_FORMID) 0 $d.ToArray()
}


function AddAlias($d, [int]$index, [string]$name, [int]$packageFormID) {
    AddSub $d 'ALST' ([BitConverter]::GetBytes([uint32]$index))
    AddSub $d 'ALID' (ZStr $name)
    AddSub $d 'FNAM' ([BitConverter]::GetBytes([uint32]0x202))   
    if ($packageFormID -ge 0) {
        $slot = if ($index -lt 5) { $index } else { $index - 5 }
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32](0x01000850 + $slot)))
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32](0x01000840 + $slot)))
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32](0x01000870 + $slot)))
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32](0x01000820 + $slot)))
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32](0x01000810 + $slot)))
        AddSub $d 'ALPC' ([BitConverter]::GetBytes([uint32]$packageFormID))
    }
    AddSub $d 'VTCK' ([BitConverter]::GetBytes([uint32]0))
    AddSub $d 'ALED' @()
}





function MakeVmad([string]$scriptName) {
    $v = NewBuf
    AddU16 $v 5                        
    AddU16 $v 2                        
    AddU16 $v 1                        
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($scriptName)
    AddU16 $v ([uint16]$nameBytes.Length)
    AddBytes $v $nameBytes             
    [void]$v.Add([byte]0)              
    AddU16 $v 0                        
    return $v.ToArray()
}

function MakeQuest {
    $d = NewBuf
    AddSub $d 'EDID' (ZStr 'WayfarerQuest')
    
    AddSub $d 'VMAD' (MakeVmad 'WayfarerQuestScript')
    AddSub $d 'FULL' (ZStr 'Walk With Me')
    $dnam = NewBuf
    AddU16 $dnam 0x0019                
    [void]$dnam.Add([byte]$QUEST_PRIORITY)
    [void]$dnam.Add([byte]0xFF)
    AddU32 $dnam 0
    AddU32 $dnam 0                     
    AddSub $d 'DNAM' $dnam.ToArray()
    AddSub $d 'NEXT' @()
    AddSub $d 'ANAM' ([BitConverter]::GetBytes([uint32]($SLOTS * 4)))   

    
    for ($i = 0; $i -lt 5; $i++) { AddAlias $d (FollowerAliasID $i) "WayfarerFollower$i" (PackageID $i) }
    for ($i = 0; $i -lt 5; $i++) { AddAlias $d (MarkerAliasID $i) "WayfarerMarker$i" -1 }
    for ($i = 5; $i -lt $SLOTS; $i++) { AddAlias $d (FollowerAliasID $i) "WayfarerFollower$i" (PackageID $i) }
    for ($i = 5; $i -lt $SLOTS; $i++) { AddAlias $d (MarkerAliasID $i) "WayfarerMarker$i" -1 }

    for ($i = 0; $i -lt $SLOTS; $i++) { AddAlias $d (20+$i) "WayfarerGroundSeat$i" -1 }

    for ($i = 0; $i -lt $SLOTS; $i++) { AddAlias $d (30+$i) "WayfarerGatherMarker$i" -1 }

    return MakeRecord 'QUST' ([uint32]$QUST_FORMID) 0 $d.ToArray()
}


function MakeHeader([int]$recordCount) {
    $d = NewBuf
    $hedr = NewBuf
    AddBytes $hedr ([BitConverter]::GetBytes([float]1.7))
    AddU32   $hedr ([uint32]$recordCount)
    AddU32   $hedr ([uint32]0x887) 
    AddSub $d 'HEDR' $hedr.ToArray()
    AddSub $d 'CNAM' (ZStr 'Tobih')
    AddSub $d 'SNAM' (ZStr 'Walk With Me - travelling companions and contextual rest')
    AddSub $d 'MAST' (ZStr 'Skyrim.esm')
    AddSub $d 'DATA' ([byte[]](0,0,0,0,0,0,0,0))
    return MakeRecord 'TES4' 0 0x200 $d.ToArray()   
}


$packPayload = NewBuf
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakePackage $i) }
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakeSandboxPackage $i) }
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakeSandboxPackage $i $true) }
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakeSandboxPackage $i $false $true) }
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakeSeatPackage $i) }
for ($i = 0; $i -lt $SLOTS; $i++) { AddBytes $packPayload (MakeGatherPackage $i) }
$packGroup = MakeGroup 'PACK' $packPayload.ToArray()

$questGroup = MakeGroup 'QUST' (MakeQuest)

$file = NewBuf
AddBytes $file (MakeHeader ($SLOTS * 8 + 9))
AddBytes $file $packGroup
AddBytes $file $questGroup
$globals=NewBuf;AddBytes $globals (MakeSandboxGlobal)
for($i=0;$i -lt $SLOTS;$i++){
    $g=NewBuf;AddSub $g 'EDID' (ZStr "WayfarerSocialActive$i");AddSub $g 'FNAM' ([byte[]](0x66));AddSub $g 'FLTV' ([BitConverter]::GetBytes([float]0))
    AddBytes $globals (MakeRecord 'GLOB' ([uint32](0x01000830+$i)) 0 $g.ToArray())
}
for($i=0;$i -lt $SLOTS;$i++){
    $g=NewBuf;AddSub $g 'EDID' (ZStr "WayfarerPoseActive$i");AddSub $g 'FNAM' ([byte[]](0x66));AddSub $g 'FLTV' ([BitConverter]::GetBytes([float]0))
    AddBytes $globals (MakeRecord 'GLOB' ([uint32](0x01000860+$i)) 0 $g.ToArray())
}
AddBytes $file (MakeGroup 'GLOB' $globals.ToArray())




$faction = NewBuf
AddSub $faction 'EDID' (ZStr 'WayfarerDialogueState')
AddSub $faction 'DATA' ([BitConverter]::GetBytes([uint32]1)) 
AddBytes $file (MakeGroup 'FACT' (MakeRecord 'FACT' 0x01000880 0 $faction.ToArray()))

function AddWString($buf, [string]$value) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($value)
    AddU16 $buf ([uint16]$bytes.Length); AddBytes $buf $bytes
}
function MakeDialogueVmad([string]$scriptName) {
    $v = NewBuf
    AddBytes $v (MakeVmad $scriptName)
    AddBytes $v ([byte[]](2,1)) 
    AddWString $v $scriptName
    [void]$v.Add([byte]1) 
    AddWString $v $scriptName
    AddWString $v 'Fragment_0'
    return $v.ToArray()
}
$branches = NewBuf
$topics = NewBuf
for ($action = 0; $action -lt 2; $action++) {
    $branchID = [uint32](0x01000881 + $action)
    $topicID = [uint32](0x01000883 + $action)
    $infoID = [uint32](0x01000885 + $action)
    $verb = if ($action -eq 0) { 'Add' } else { 'Remove' }
    $prompt = if ($action -eq 0) { '[Walk With Me] Walk with me.' } else { '[Walk With Me] Follow your own lead.' }
    $b = NewBuf
    AddSub $b 'EDID' (ZStr "WayfarerDialogue${verb}Branch")
    AddSub $b 'QNAM' ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    AddSub $b 'TNAM' ([BitConverter]::GetBytes([uint32]0))
    AddSub $b 'DNAM' ([BitConverter]::GetBytes([uint32]1)) 
    AddSub $b 'SNAM' ([BitConverter]::GetBytes($topicID))
    AddBytes $branches (MakeRecord 'DLBR' $branchID 0 $b.ToArray())
    $t = NewBuf
    AddSub $t 'EDID' (ZStr "WayfarerDialogue${verb}Topic")
    AddSub $t 'FULL' (ZStr $prompt)
    AddSub $t 'PNAM' ([BitConverter]::GetBytes([float]50))
    AddSub $t 'BNAM' ([BitConverter]::GetBytes($branchID))
    AddSub $t 'QNAM' ([BitConverter]::GetBytes([uint32]$QUST_FORMID))
    AddSub $t 'DATA' ([byte[]](0,0,0,0)) 
    AddSub $t 'SNAM' ([System.Text.Encoding]::ASCII.GetBytes('CUST'))
    AddSub $t 'TIFC' ([BitConverter]::GetBytes([uint32]1))
    AddBytes $topics (MakeRecord 'DIAL' $topicID 0 $t.ToArray())
    $info = NewBuf
    AddSub $info 'VMAD' (MakeDialogueVmad "WayfarerDialogue$verb")
    AddSub $info 'ENAM' ([byte[]](1,0,0,0)) 
    AddSub $info 'TPIC' ([BitConverter]::GetBytes($topicID))
    AddSub $info 'PNAM' ([BitConverter]::GetBytes([uint32]0))
    AddSub $info 'CNAM' ([byte[]](0))
    $condition = NewBuf
    AddU32 $condition 0 
    $rank = if ($action -eq 0) { 0 } else { 2 }
    AddBytes $condition ([BitConverter]::GetBytes([float]$rank))
    AddU32 $condition 73 
    AddU32 $condition 0x01000880; AddU32 $condition 0
    AddU32 $condition 0; AddU32 $condition 0; AddU32 $condition ([uint32]::MaxValue)
    AddSub $info 'CTDA' $condition.ToArray()
    $record = MakeRecord 'INFO' $infoID 0 $info.ToArray()
    
    $g = NewBuf
    AddSig $g 'GRUP'; AddU32 $g ([uint32]($record.Length + 24)); AddU32 $g $topicID
    AddU32 $g 7; AddU32 $g 0; AddU32 $g 0; AddBytes $g $record
    AddBytes $topics $g.ToArray()
}
AddBytes $file (MakeGroup 'DLBR' $branches.ToArray())
AddBytes $file (MakeGroup 'DIAL' $topics.ToArray())

[System.IO.File]::WriteAllBytes($Out, $file.ToArray())

$seqDirectory = Join-Path (Split-Path $Out) 'SEQ'
[void][System.IO.Directory]::CreateDirectory($seqDirectory)
[System.IO.File]::WriteAllBytes((Join-Path $seqDirectory 'Wayfarer.seq'), [BitConverter]::GetBytes([uint32]$QUST_FORMID))
Write-Host "wrote $Out ($($file.Count) bytes)"
Write-Host '  Sandbox: 810..819; Social: 820..829; globals: 80B, 830..839; Travel: 800..804, 806..80A; quest: 805; poses: 840..849; seats: 850..859; pose globals: 860..869; gather: 870..879; aliases: 40 (old IDs preserved)'
