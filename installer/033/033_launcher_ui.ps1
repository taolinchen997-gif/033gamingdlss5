$script:YYArtworkPath=Join-Path $PSScriptRoot 'assets/yanyun-approved-installer.png'
$script:YYArtwork=$null
function Draw-033Text($Graphics,[string]$Text,[float]$Size,[string]$Color,$Bounds,
                       [bool]$Center=$false,[bool]$Bold=$false) {
    $style=if($Bold){[Drawing.FontStyle]::Bold}else{[Drawing.FontStyle]::Regular}
    $font=[Drawing.Font]::new('Microsoft YaHei UI',$Size,$style,[Drawing.GraphicsUnit]::Pixel)
    $ink=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($Color))
    $format=[Drawing.StringFormat]::new()
    $format.LineAlignment='Center';$format.Trimming='EllipsisCharacter'
    $format.FormatFlags=[Drawing.StringFormatFlags]::NoWrap
    if($Center){$format.Alignment='Center'}
    try{$Graphics.DrawString($Text,$font,$ink,[Drawing.RectangleF]::new($Bounds[0],$Bounds[1],$Bounds[2],$Bounds[3]),$format)}
    finally{$format.Dispose();$ink.Dispose();$font.Dispose()}
}
# A short paragraph that wraps inside its box, centred (the owner's notice).
function Draw-033Paragraph($Graphics,[string]$Text,[float]$Size,[string]$Color,$Bounds){
    $font=[Drawing.Font]::new('Microsoft YaHei UI',$Size,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
    $ink=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($Color))
    $format=[Drawing.StringFormat]::new()
    $format.Alignment='Center';$format.LineAlignment='Center';$format.Trimming='EllipsisWord'
    try{$Graphics.DrawString($Text,$font,$ink,[Drawing.RectangleF]::new($Bounds[0],$Bounds[1],$Bounds[2],$Bounds[3]),$format)}
    finally{$format.Dispose();$ink.Dispose();$font.Dispose()}
}
# A game path keeps its start and its end; the middle gives way (EllipsisPath).
function Draw-033PathText($Graphics,[string]$Text,[float]$Size,[string]$Color,$Bounds){
    $font=[Drawing.Font]::new('Microsoft YaHei UI',$Size,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
    $ink=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($Color))
    $format=[Drawing.StringFormat]::new()
    $format.LineAlignment='Center';$format.Trimming='EllipsisPath';$format.FormatFlags=[Drawing.StringFormatFlags]::NoWrap
    try{$Graphics.DrawString($Text,$font,$ink,[Drawing.RectangleF]::new($Bounds[0],$Bounds[1],$Bounds[2],$Bounds[3]),$format)}
    finally{$format.Dispose();$ink.Dispose();$font.Dispose()}
}

function Draw-033Brand($Graphics) {
    # Same polygon geometry as the current game panel's original 033 mark.
    $saved=$Graphics.Save()
    $gold=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($(if($script:YYLight){'#1B252C'}else{'#EAD5AA'})))
    $path=[Drawing.Drawing2D.GraphicsPath]::new()
    try {
        $Graphics.TranslateTransform(18,11);$Graphics.ScaleTransform(.52,.52)
        $outer=@(@(10,0),@(38,0),@(48,10),@(48,44),@(38,54),@(10,54),@(0,44),@(0,10))
        $inner=@(@(15,11),@(33,11),@(36,14),@(36,40),@(33,43),@(15,43),@(12,40),@(12,14))
        foreach($shape in @($outer,$inner)){
            $points=[Drawing.PointF[]]@($shape|ForEach-Object{[Drawing.PointF]::new($_[0],$_[1])})
            $path.AddPolygon($points)
        }
        $Graphics.FillPath($gold,$path)
        $three=@(@(0,0),@(49,0),@(49,10),@(36,22),@(49,32),@(49,44),@(39,54),@(0,54),
                 @(12,42),@(33,42),@(36,39),@(36,33),@(16,33),@(26,22),@(35,13),@(9,13))
        foreach($offset in @(55,110)){
            $points=[Drawing.PointF[]]@($three|ForEach-Object{[Drawing.PointF]::new(($offset+$_[0]),$_[1])})
            $Graphics.FillPolygon($gold,$points)
        }
    }finally{$path.Dispose();$gold.Dispose();$Graphics.Restore($saved)}
}

$script:YYLight=$false;$script:YYEnglish=$false;$script:YYProgress=$null
function YYText([string]$Zh,[string]$En){if($script:YYEnglish){$En}else{$Zh}}
function Read-033Appearance {
 try{$p=Join-Path $env:LOCALAPPDATA '033YanYunRuntime/appearance.cfg';if(Test-Path -LiteralPath $p){$lines=[IO.File]::ReadAllLines($p);$script:YYLight=$lines -contains 'light=1';$script:YYEnglish=$lines -contains 'english=1'}}catch{}
}
function Save-033Appearance {
 $dir=Join-Path $env:LOCALAPPDATA '033YanYunRuntime';[void][IO.Directory]::CreateDirectory($dir);$p=Join-Path $dir 'appearance.cfg';$temp=$p+'.'+$PID+'.tmp'
 [IO.File]::WriteAllText($temp,('schema=1'+"`nlight="+[int]$script:YYLight+"`nenglish="+[int]$script:YYEnglish+"`n"),[Text.UTF8Encoding]::new($false))
 Move-Item -LiteralPath $temp -Destination $p -Force
}
function Get-033Palette {
 if($script:YYLight){return @{Bg='#F3F6F8';Card='#FBFCFD';Text='#1B252C';Muted='#58636C';Edge='#CCD2D7';Gold='#9C763D';Track='#DEE2E6';Selected='#E6D5B9'}}
 return @{Bg='#141C1F';Card='#192124';Text='#F0EEE6';Muted='#B4BAB8';Edge='#4C5352';Gold='#E5C78D';Track='#354044';Selected='#403B2C'}
}
# S40 (owner 2026-09-24: 「燕云安装器直接以海报形式出现两个，一个国服，一个国际服，点按安装？」, then 「可以」):
# one poster card per client. The posters are crops of the approved artwork (night for 国服, day for 国际服);
# no new illustration. Each card installs, restores and relocates its own client; 净化后安装 is 国服 only.
function Get-033Editions {
 return @(
  @{Id='cn';Exe='yysls.exe';Night=$true;X=18;Title=@('国服','China');Subtitle='燕云十六声';Clean=$true
    Note=@('「净化后安装」只在国服可用','"Clean install" is for the China version only')},
  @{Id='intl';Exe='wwm.exe';Night=$false;X=376;Title=@('国际服','International');Subtitle='Where Winds Meet · Steam';Clean=$false
    Note=@('国际服要用 DirectX 12 启动游戏才有 DLSS','Launch the game with DirectX 12 for DLSS')}
 )
}
function Get-033Edition([string]$Id){foreach($e in Get-033Editions){if($e.Id -eq $Id){return $e}};return $null}
function Get-033EditionOf([string]$Exe){
 if(-not $Exe){return $null}
 $leaf=[IO.Path]::GetFileName($Exe)
 foreach($e in Get-033Editions){if($leaf -ieq $e.Exe){return $e.Id}}
 return $null
}
# The client's own folder (the one holding Engine), shown on its card.
function Get-033EditionFolder([string]$Exe){
 if(-not $Exe){return ''}
 $root=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $Exe)))
 if($root){return $root}else{return $Exe}
}
function Split-033YanYunDetections([string[]]$Detected){
 $split=@{cn=@();intl=@()}
 foreach($exe in @($Detected|Where-Object {$_}|Sort-Object -Unique)){$id=Get-033EditionOf $exe;if($id){$split[$id]=@($split[$id])+@($exe)}}
 return $split
}
# A path chosen for one card. Returns @{Edition;Exe;Moved}: a client picked on the other client's card goes to its own card.
function Resolve-033EditionPath([string]$Path,[string]$Edition){
 if(Test-Path -LiteralPath $Path -PathType Container){
  $found=@(Find-033YanYunInstallations @($Path))
  $mine=@($found|Where-Object {(Get-033EditionOf $_) -eq $Edition})
  if($mine.Count -eq 1){$Path=$mine[0]}
  elseif($found.Count -eq 1){$Path=$found[0]}
  else{$e=Get-033Edition $Edition;throw (YYText ('请选择准确的 '+$e.Exe) ('Select the exact '+$e.Exe))}
 }
 $full=[IO.Path]::GetFullPath($Path)
 [void](Get-033YanYunTargets @($full))
 $own=Get-033EditionOf $full
 return @{Edition=$own;Exe=$full;Moved=($own -ne $Edition)}
}
function Get-033LauncherLayout {
 $buttons=@()
 $tab=1
 foreach($e in Get-033Editions){
  $x=$e.X
  $buttons+=@(
   @{Id=('poster-'+$e.Id);Edition=$e.Id;Bounds=@(($x+10),124,326,270);Style='poster';Tab=$tab;TabStop=$false},
   @{Id=('browse-'+$e.Id);Edition=$e.Id;Bounds=@(($x+252),404,80,26);Style='smalllink';Size=15;Tab=($tab+1)},
   @{Id=('install-'+$e.Id);Edition=$e.Id;Bounds=@(($x+14),442,318,64);Style='primary';Tab=($tab+2)},
   @{Id=('restore-'+$e.Id);Edition=$e.Id;Bounds=@(($x+14),522,$(if($e.Clean){152}else{318}),30);Style='smalllink';Size=18;Tab=($tab+3)}
  )
  if($e.Clean){$buttons+=@(@{Id=('clean-'+$e.Id);Edition=$e.Id;Bounds=@(($x+180),522,152,30);Style='smalllink';Size=18;Tab=($tab+4)})}
  $tab+=5
 }
 $buttons+=@(
  @{Id='dark';Text='黑';Bounds=@(386,14,56,29);Style='choice';Tab=11},
  @{Id='light';Text='白';Bounds=@(448,14,56,29);Style='choice';Tab=12},
  @{Id='zh';Text='中';Bounds=@(520,14,50,29);Style='choice';Tab=13},
  @{Id='en';Text='EN';Bounds=@(576,14,50,29);Style='choice';Tab=14},
  @{Id='minimize';Text='最小化';Bounds=@(646,14,28,29);Style='system';Tab=15},
  @{Id='close';Text='关闭';Bounds=@(694,14,28,29);Style='system';Tab=16},
  @{Id='video';Bounds=@(38,744,664,28);Style='smalllink';Size=15;Tab=17},
  @{Id='home';Bounds=@(200,778,340,30);Style='smalllink';Size=16;Tab=18})
 return @{Width=740;Height=816;Card=@(114,346,500);Error=@(36,616,668,72);Notice=@(38,692,664,52);Buttons=$buttons}
}
function Get-033ButtonText([string]$Id){
 $base,$edition=$Id -split '-',2
 switch($base){
  'browse'{YYText '更改位置' 'Change'}'install'{YYText '安装 / 升级' 'Install / Update'}'restore'{YYText '还原安装前' 'Restore original'}'clean'{YYText '净化后安装' 'Clean install'}
  'poster'{$e=Get-033Edition $edition;YYText $e.Title[0] $e.Title[1]}
  'dark'{YYText '黑' 'Dark'}'light'{YYText '白' 'Light'}'zh'{'中'}'en'{'EN'}'minimize'{YYText '最小化' 'Minimize'}'close'{YYText '关闭' 'Close'}
  'video'{$script:K033VideoUrl}'home'{YYText '作者主页 B站@热心网友033' 'Author page: Bilibili @热心网友033'}
 }
}
function Get-033NoticeText{
 return (YYText ('本压缩包仅用于交流学习，禁止用于盈利。'+[char]10+'二次分发可能导致安装器损坏，请到作者B站视频主页下载：') 'For learning and exchange only; not for profit. Copies passed on by others can break the installer. Download it from the author''s Bilibili video page:')
}
function New-033RoundPath($Bounds,[float]$Radius=9){
 $x=[float]$Bounds[0];$y=[float]$Bounds[1];$w=[float]$Bounds[2];$h=[float]$Bounds[3];$d=[Math]::Min(2*$Radius,[Math]::Min($w,$h))
 $path=[Drawing.Drawing2D.GraphicsPath]::new();$path.AddArc($x,$y,$d,$d,180,90);$path.AddArc(($x+$w-$d),$y,$d,$d,270,90);$path.AddArc(($x+$w-$d),($y+$h-$d),$d,$d,0,90);$path.AddArc($x,($y+$h-$d),$d,$d,90,90);$path.CloseFigure();return $path
}
function Draw-033Poster($Graphics,$Bounds,$Edition,[bool]$Hover=$false,[bool]$Focused=$false){
 # A crop of the approved artwork's right half (moon or sun, pagoda, swordsman); its baked titles sit on the left half.
 $x=[float]$Bounds[0];$y=[float]$Bounds[1];$w=[float]$Bounds[2];$h=[float]$Bounds[3]
 if(!$script:YYArtwork){$script:YYArtwork=[Drawing.Image]::FromFile($script:YYArtworkPath)}
 $sourceX=if($Edition.Night){452}else{1204}
 $clip=New-033RoundPath @($x,$y,$w,$h) 8;$saved=$Graphics.Save()
 try{
  $Graphics.SetClip($clip);$Graphics.InterpolationMode='HighQualityBicubic'
  $Graphics.DrawImage($script:YYArtwork,[Drawing.Rectangle]::new([int]$x,[int]$y,[int]$w,[int]$h),$sourceX,186,300,[int](300*$h/$w),[Drawing.GraphicsUnit]::Pixel)
  $band=[Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.RectangleF]::new($x,($y+$h-96),$w,96),[Drawing.Color]::FromArgb(0,10,14,16),[Drawing.Color]::FromArgb(215,10,14,16),90.0)
  try{$Graphics.FillRectangle($band,$x,($y+$h-96),$w,96)}finally{$band.Dispose()}
  Draw-033Text $Graphics (YYText $Edition.Title[0] $Edition.Title[1]) 34 '#E5C78D' @(($x+18),($y+$h-86),($w-36),44) $false $true
  Draw-033Text $Graphics $Edition.Subtitle 17 '#F0EEE6' @(($x+20),($y+$h-44),($w-40),28)
 }finally{$Graphics.Restore($saved);$clip.Dispose()}
 if($Hover -or $Focused){
  $c=Get-033Palette;$pen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($c.Gold),2);$frame=New-033RoundPath @(($x+1),($y+1),($w-2),($h-2)) 8
  try{$Graphics.DrawPath($pen,$frame)}finally{$pen.Dispose();$frame.Dispose()}
 }
}
function Draw-033SmallLink($Graphics,[string]$Text,$Bounds,[float]$Size=15,[bool]$Hover=$false){
 $c=Get-033Palette;Draw-033Text $Graphics $Text $Size $c.Gold $Bounds $true
 $font=[Drawing.Font]::new('Microsoft YaHei UI',$Size,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel);$w=$Graphics.MeasureString($Text,$font).Width;$font.Dispose()
 $pen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($c.Gold),$(if($Hover){2}else{1}))
 try{$cx=$Bounds[0]+$Bounds[2]/2;$y=$Bounds[1]+$Bounds[3]/2+$Size*.62;$Graphics.DrawLine($pen,($cx-$w/2+3),$y,($cx+$w/2-3),$y)}finally{$pen.Dispose()}
}
function Draw-033LauncherButton($Graphics,$Spec,$Bounds,[bool]$Hover=$false,[bool]$Focused=$false,[bool]$Missing=$false){
 $c=Get-033Palette;$x=[float]$Bounds[0];$y=[float]$Bounds[1];$w=[float]$Bounds[2];$h=[float]$Bounds[3]
 $under=if($Spec.Style -eq 'smalllink' -and -not $Spec.ContainsKey('Edition')){$c.Bg}else{$c.Card}
 $buttonBack=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($under));try{$Graphics.FillRectangle($buttonBack,$x,$y,$w,$h)}finally{$buttonBack.Dispose()}
 if($Spec.Style -eq 'poster'){Draw-033Poster $Graphics $Bounds (Get-033Edition $Spec.Edition) $Hover $Focused;return}
 if($Spec.Style -eq 'smalllink'){Draw-033SmallLink $Graphics (Get-033ButtonText $Spec.Id) $Bounds $Spec.Size ($Hover -or $Focused);return}
 $edge=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($(if($Hover -or $Focused){$c.Gold}else{$c.Edge})),1)
 $path=New-033RoundPath @(($x+1),($y+1),($w-2),($h-2)) 8;$fill=$null
 try{
  if($Spec.Style -eq 'system'){
   $edge.Color=[Drawing.ColorTranslator]::FromHtml($c.Text)
   if($Spec.Id -eq 'close'){$Graphics.DrawLine($edge,($x+8),($y+8),($x+$w-8),($y+$h-8));$Graphics.DrawLine($edge,($x+$w-8),($y+8),($x+8),($y+$h-8))}
   else{$Graphics.DrawLine($edge,($x+8),($y+$h/2),($x+$w-8),($y+$h/2))};return
  }
  if($Spec.Style -eq 'primary' -and $Missing){
   # This client was not found: the card's button asks for its location instead.
   $fill=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($(if($Hover){$c.Selected}else{$c.Card})));$Graphics.FillPath($fill,$path)
   $edge.Color=[Drawing.ColorTranslator]::FromHtml($c.Gold);$Graphics.DrawPath($edge,$path)
   Draw-033Text $Graphics (YYText '先选择游戏位置' 'Choose the game location') 22 $c.Gold $Bounds $true $true;return
  }
  $selected=($Spec.Id -eq 'dark' -and !$script:YYLight) -or ($Spec.Id -eq 'light' -and $script:YYLight) -or ($Spec.Id -eq 'zh' -and !$script:YYEnglish) -or ($Spec.Id -eq 'en' -and $script:YYEnglish)
  if($Spec.Style -eq 'primary'){$fill=[Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.RectangleF]::new($x,$y,$w,$h),[Drawing.ColorTranslator]::FromHtml('#F9DFAB'),[Drawing.ColorTranslator]::FromHtml('#C39C5E'),90.0)}
  else{$fill=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($(if($selected -or $Hover){$c.Selected}else{$c.Card})))}
  $Graphics.FillPath($fill,$path);if($Spec.Style -ne 'link'){$Graphics.DrawPath($edge,$path)}else{$Graphics.DrawLine($edge,($x+56),($y+$h-5),($x+$w-56),($y+$h-5))}
  $size=if($Spec.Style -eq 'primary'){32}elseif($Spec.Style -eq 'choice'){15}else{20}
  $ink=if($Spec.Style -eq 'primary'){'#191A15'}elseif($Spec.Style -eq 'link' -or $selected){$c.Gold}else{$c.Text}
  Draw-033Text $Graphics (Get-033ButtonText $Spec.Id) $size $ink $Bounds $true ($Spec.Style -eq 'primary')
  if($Focused){$Graphics.DrawLine($edge,($x+12),($y+$h-5),($x+$w-12),($y+$h-5))}
 }finally{if($fill){$fill.Dispose()};$path.Dispose();$edge.Dispose()}
}
function Get-033StageText([string]$Stage){switch($Stage){
 'verify'{YYText '正在验真安装包' 'Verifying package'}'check'{YYText '正在检查燕云与安装记录' 'Checking game and installation'}'backup'{YYText '正在备份原文件' 'Backing up original files'}'write'{YYText '正在写入文件' 'Writing files'}'validate'{YYText '正在核对安装结果' 'Validating files'}'complete'{YYText '操作完成' 'Complete'}'failed'{YYText '未完成，原件与记录已保留' 'Failed; originals and records are retained'}default{YYText '正在准备' 'Preparing'}
}}
function Get-033ProgressValue($Progress){
 if(!$Progress){return -1};$fraction=if($Progress.Total -gt 0){[Math]::Min(1.0,[Math]::Max(0.0,([double]$Progress.Done/$Progress.Total)))}else{0}
 switch([string]$Progress.Stage){'backup'{return (15+35*$fraction)}'write'{return (50+40*$fraction)}'validate'{return 95}'complete'{return 100}default{return -1}}
}
function Draw-033Launcher($Graphics,$Layout,[string]$Domestic,[string]$International,[bool]$IncludeControls=$false){
 $c=Get-033Palette;$Graphics.SmoothingMode='AntiAlias';$Graphics.TextRenderingHint='AntiAliasGridFit';$Graphics.Clear([Drawing.ColorTranslator]::FromHtml($c.Bg))
 $edge=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($c.Edge),1);$card=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Card));$outer=New-033RoundPath @(1,1,($Layout.Width-2),($Layout.Height-2)) 12
 $exes=@{cn=$Domestic;intl=$International}
 try{
  $saved=$Graphics.Save();try{$Graphics.TranslateTransform(2,6);Draw-033Brand $Graphics}finally{$Graphics.Restore($saved)}
  Draw-033Text $Graphics (YYText '燕云定制版' 'YANYUN EDITION') 18 $c.Text @(115,16,260,34) $false $true
  Draw-033Text $Graphics (YYText '选择你的燕云，点一下就装' 'Choose your version') 24 $c.Text @(38,66,440,40) $false $true
  Draw-033Text $Graphics (YYText '安装前请退出游戏' 'Close the game first') 15 $c.Muted @(490,66,220,40) $true
  foreach($e in Get-033Editions){
   $x=$e.X;$box=New-033RoundPath @($x,$Layout.Card[0],$Layout.Card[1],$Layout.Card[2]) 10
   try{$Graphics.FillPath($card,$box);$Graphics.DrawPath($edge,$box)}finally{$box.Dispose()}
   $exe=$exes[$e.Id]
   if($exe){
    Draw-033Text $Graphics (YYText '已找到' 'Found') 15 $c.Gold @(($x+14),404,56,26) $false $true
    Draw-033PathText $Graphics (Get-033EditionFolder $exe) 14 $c.Text @(($x+68),404,180,26)
   }else{Draw-033Text $Graphics (YYText '这台电脑上没找到' 'Not found on this PC') 15 $c.Muted @(($x+14),404,230,26)}
   Draw-033Text $Graphics (YYText $e.Note[0] $e.Note[1]) 13 $c.Muted @(($x+14),566,318,26) $true
  }
  $stage=if($script:YYProgress){Get-033StageText $script:YYProgress.Stage}else{YYText '安装时自动备份，点「还原安装前」原样放回' 'Backed up automatically; Restore original puts everything back'}
  Draw-033Text $Graphics $stage 16 $c.Text @(36,624,668,28) $true
  if($script:YYProgress){
   $bar=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Track));$fill=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Gold))
   try{$Graphics.FillRectangle($bar,76,658,588,7);$value=Get-033ProgressValue $script:YYProgress;if($value -ge 0){$Graphics.FillRectangle($fill,76,658,([float](588*$value/100)),7)}else{$Graphics.FillRectangle($fill,76,658,120,7)}}finally{$bar.Dispose();$fill.Dispose()}
   $detail=if($script:YYProgress.Total -gt 0){[string]$script:YYProgress.Done+' / '+$script:YYProgress.Total}else{[string]$script:YYProgress.Detail}
   Draw-033Text $Graphics $detail 12 $c.Muted @(38,668,664,18) $true
  }
  # S41 (owner 2026-09-24): the notice, then the video link and the author home page (both controls).
  Draw-033Paragraph $Graphics (Get-033NoticeText) 15 $c.Text $Layout.Notice
  $Graphics.DrawPath($edge,$outer)
  if($IncludeControls){
   foreach($spec in $Layout.Buttons){$saved=$Graphics.Save();try{$b=$spec.Bounds;$Graphics.SetClip([Drawing.RectangleF]::new($b[0],$b[1],$b[2],$b[3]));Draw-033LauncherButton $Graphics $spec $b $false $false ($spec.Style -eq 'primary' -and -not $exes[$spec.Edition])}finally{$Graphics.Restore($saved)}}
  }
 }finally{$card.Dispose();$edge.Dispose();$outer.Dispose()}
}

function Select-033Folder([IntPtr]$Owner,[string]$Title,[string]$Initial){
    # Explorer-style folder picker (the Vista IFileOpenDialog with FOS_PICKFOLDERS), the
    # same kind of window as the .exe picker. Windows PowerShell 5.1 / .NET Framework
    # only offers the old tree-view "浏览文件夹" box, so the COM dialog is declared
    # here; if that cannot be compiled the old box is used instead. Returns $null on cancel.
    if(-not ('W.FolderPicker' -as [type])){
        try{
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace W {
  [ComImport, Guid("42f85136-db7e-439c-85f1-e4075d135fc8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IFileDialog {
    [PreserveSig] int Show(IntPtr parent);
    void SetFileTypes(uint cFileTypes, IntPtr rgFilterSpec);
    void SetFileTypeIndex(uint iFileType);
    void GetFileTypeIndex(out uint piFileType);
    void Advise(IntPtr pfde, out uint pdwCookie);
    void Unadvise(uint dwCookie);
    void SetOptions(uint fos);
    void GetOptions(out uint pfos);
    void SetDefaultFolder(IShellItem psi);
    void SetFolder(IShellItem psi);
    void GetFolder(out IShellItem ppsi);
    void GetCurrentSelection(out IShellItem ppsi);
    void SetFileName([MarshalAs(UnmanagedType.LPWStr)] string pszName);
    void GetFileName([MarshalAs(UnmanagedType.LPWStr)] out string pszName);
    void SetTitle([MarshalAs(UnmanagedType.LPWStr)] string pszTitle);
    void SetOkButtonLabel([MarshalAs(UnmanagedType.LPWStr)] string pszText);
    void SetFileNameLabel([MarshalAs(UnmanagedType.LPWStr)] string pszLabel);
    void GetResult(out IShellItem ppsi);
    void AddPlace(IShellItem psi, uint fdap);
    void SetDefaultExtension([MarshalAs(UnmanagedType.LPWStr)] string pszDefaultExtension);
    void Close(int hr);
    void SetClientGuid(ref Guid guid);
    void ClearClientData();
    void SetFilter(IntPtr pFilter);
  }
  [ComImport, Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IShellItem {
    void BindToHandler(IntPtr pbc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
    void GetParent(out IShellItem ppsi);
    void GetDisplayName(uint sigdnName, [MarshalAs(UnmanagedType.LPWStr)] out string ppszName);
    void GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
    void Compare(IShellItem psi, uint hint, out int piOrder);
  }
  [ComImport, Guid("DC1C5A9C-E88A-4dde-A5A1-60F82A20AEF7")] public class FileOpenDialogRCW {}
  public static class FolderPicker {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
    static extern void SHCreateItemFromParsingName(string pszPath, IntPtr pbc, ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IShellItem ppv);
    public static string Pick(IntPtr owner, string title, string okLabel, string initial) {
      IFileDialog dlg = (IFileDialog)new FileOpenDialogRCW();
      uint opts; dlg.GetOptions(out opts);
      dlg.SetOptions(opts | 0x20u | 0x40u | 0x8u); // FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR
      if (!string.IsNullOrEmpty(title)) dlg.SetTitle(title);
      if (!string.IsNullOrEmpty(okLabel)) dlg.SetOkButtonLabel(okLabel);
      if (!string.IsNullOrEmpty(initial) && System.IO.Directory.Exists(initial)) {
        try { Guid iid = typeof(IShellItem).GUID; IShellItem item; SHCreateItemFromParsingName(initial, IntPtr.Zero, ref iid, out item); dlg.SetFolder(item); } catch { }
      }
      int hr = dlg.Show(owner);
      if (hr != 0) return null;
      IShellItem result; dlg.GetResult(out result);
      string path; result.GetDisplayName(0x80058000u, out path); // SIGDN_FILESYSPATH
      return path;
    }
  }
}
'@ -ErrorAction Stop
        }catch{}
    }
    if('W.FolderPicker' -as [type]){
        return [W.FolderPicker]::Pick($Owner,$Title,'选这个文件夹',$Initial)
    }
    $dialog=[Windows.Forms.FolderBrowserDialog]::new()
    try{
        $dialog.Description=$Title;$dialog.ShowNewFolderButton=$false
        if($Initial -and (Test-Path -LiteralPath $Initial -PathType Container)){$dialog.SelectedPath=$Initial}
        if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return $dialog.SelectedPath}
        return $null
    }finally{$dialog.Dispose()}
}


function New-033WorkerCommand([string]$Script,[string]$Exe,[string]$Action,[string]$Progress,[string]$Log,[switch]$Clean){
 function Q([string]$Value){return "'"+$Value.Replace("'","''")+"'"}
 $command="& "+(Q $Script)+" -GameExe "+(Q $Exe)+" -Action "+(Q $Action)+$(if($Clean){" -Clean"}else{""})+" -NoSplash -Silent -NoElevate -ProgressFile "+(Q $Progress)+" *> "+(Q $Log)+"; exit `$LASTEXITCODE"
 return [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
}
# S41: the author's video (where the package is published) and home page, opened in the default browser.
$script:K033VideoUrl='https://www.bilibili.com/video/BV1w9aw6oE7x/'
$script:K033HomepageUrl='https://space.bilibili.com/88101991'
function Open-033Url([string]$Url){
 if($script:K033LauncherOpenUrl){& $script:K033LauncherOpenUrl $Url;return}
 [void][Diagnostics.Process]::Start($Url)
}
# Test seams (null in the product): the worker start, the 净化后安装 question, the file picker and the browser.
$script:K033LauncherStartWorker=$null;$script:K033LauncherConfirm=$null;$script:K033LauncherPickFile=$null;$script:K033LauncherOpenUrl=$null
function Show-Launcher {
 # -Exercise (tests only) runs a script block against the built window instead of showing it.
 param([string]$PreExe='',[scriptblock]$Exercise=$null)
 $form=$null;$timer=$null;$tip=$null;$uiFont=$null;$errorFont=$null;$state=$null
 try{
  Enable-DpiAware;Add-Type -AssemblyName System.Windows.Forms;Add-Type -AssemblyName System.Drawing;Read-033Appearance
  $layout=Get-033LauncherLayout
  $dpi=[Drawing.Graphics]::FromHwnd([IntPtr]::Zero);try{$scale=$dpi.DpiX/96.0}finally{$dpi.Dispose()}
  $workArea=[Windows.Forms.Screen]::PrimaryScreen.WorkingArea
  $scale=[Math]::Min($scale,[Math]::Min(($workArea.Height-24)/$layout.Height,($workArea.Width-24)/$layout.Width))
  function U([double]$Value){return [int][Math]::Round($Value*$scale)}
  $state=@{Exe=@{cn='';intl=''};Choices=@{cn=@();intl=@()};Busy=$false;Process=$null;Progress='';Log='';Action='';Edition='';Clean=$false;Drag=$false;DragPoint=$null;Failure='';Menu=$null}
  $form=[Windows.Forms.Form]::new();$form.Text='033 燕云定制版';$form.FormBorderStyle='None';$form.AutoScaleMode='None';$form.StartPosition='CenterScreen';$form.KeyPreview=$true
  $form.ClientSize=[Drawing.Size]::new((U $layout.Width),(U $layout.Height))
  $form.GetType().GetProperty('DoubleBuffered',[Reflection.BindingFlags]'Instance,NonPublic').SetValue($form,$true,$null)
  $form.Add_Paint({$save=$_.Graphics.Save();try{$_.Graphics.ScaleTransform($scale,$scale);Draw-033Launcher $_.Graphics $layout $state.Exe.cn $state.Exe.intl}finally{$_.Graphics.Restore($save)}})
  $form.Add_MouseDown({if($_.Button -eq 'Left' -and $_.Y -lt (U 54)){$state.Drag=$true;$state.DragPoint=$_.Location;$form.Capture=$true}})
  $form.Add_MouseMove({if($state.Drag){$form.Location=[Drawing.Point]::new(($form.Left+$_.X-$state.DragPoint.X),($form.Top+$_.Y-$state.DragPoint.Y))}})
  $form.Add_MouseUp({$state.Drag=$false;$form.Capture=$false});$form.Add_MouseCaptureChanged({if(!$form.Capture){$state.Drag=$false}})
  $form.Add_FormClosing({if($state.Busy){$_.Cancel=$true}})
  $form.Add_KeyDown({if($_.KeyCode -eq 'Escape' -and !$state.Busy){$form.Close()}})
  $tip=[Windows.Forms.ToolTip]::new();$uiFont=[Drawing.Font]::new('Microsoft YaHei UI',(15*$scale),[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
  $errorFont=[Drawing.Font]::new('Microsoft YaHei UI',(13*$scale),[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
  $e=$layout.Error;$errorBox=[Windows.Forms.TextBox]::new();$errorBox.SetBounds((U $e[0]),(U $e[1]),(U $e[2]),(U $e[3]));$errorBox.Font=$errorFont;$errorBox.Multiline=$true;$errorBox.ReadOnly=$true;$errorBox.ScrollBars='Vertical';$errorBox.BorderStyle='None';$errorBox.Visible=$false;$errorBox.TabStop=$false;$form.Controls.Add($errorBox)
  $buttons=@{}
  foreach($spec in $layout.Buttons){
   $button=[Windows.Forms.Button]::new();$button.Tag=@{Spec=$spec;Hover=$false};$p=$spec.Bounds;$button.SetBounds((U $p[0]),(U $p[1]),(U $p[2]),(U $p[3]));$button.FlatStyle='Flat';$button.FlatAppearance.BorderSize=0;$button.TabIndex=$spec.Tab;$button.Font=$uiFont;$button.Cursor=[Windows.Forms.Cursors]::Hand
   if($spec.ContainsKey('TabStop')){$button.TabStop=[bool]$spec.TabStop}
   $button.Add_Paint({$save=$_.Graphics.Save();try{$_.Graphics.ScaleTransform($scale,$scale);$_.Graphics.SmoothingMode='AntiAlias';$_.Graphics.TextRenderingHint='AntiAliasGridFit';$s=$this.Tag.Spec;Draw-033LauncherButton $_.Graphics $s @(0,0,($this.Width/$scale),($this.Height/$scale)) $this.Tag.Hover $this.Focused ($s.Style -eq 'primary' -and $s.ContainsKey('Edition') -and -not $state.Exe[$s.Edition])}finally{$_.Graphics.Restore($save)}})
   $button.Add_MouseEnter({$this.Tag.Hover=$true;$this.Invalidate()});$button.Add_MouseLeave({$this.Tag.Hover=$false;$this.Invalidate()});$button.Add_GotFocus({$this.Invalidate()});$button.Add_LostFocus({$this.Invalidate()})
   $buttons[$spec.Id]=$button;$form.Controls.Add($button)
  }
  $refresh={
   $colors=Get-033Palette;$form.BackColor=[Drawing.ColorTranslator]::FromHtml($colors.Bg)
   $errorBox.BackColor=[Drawing.ColorTranslator]::FromHtml($colors.Card);$errorBox.ForeColor=[Drawing.ColorTranslator]::FromHtml($colors.Text)
   foreach($id in @($buttons.Keys)){
    $b=$buttons[$id];$b.Text=Get-033ButtonText $id;$b.BackColor=$form.BackColor
    $s=$b.Tag.Spec;$b.AccessibleName=$(if($s.ContainsKey('Edition')){$b.Text+' · '+(Get-033ButtonText ('poster-'+$s.Edition))}else{$b.Text})
    if($id -notin @('dark','light','zh','en','minimize','video','home')){$b.Enabled=!$state.Busy}
    $b.Invalidate()
   }
   foreach($ed in Get-033Editions){
    $exe=$state.Exe[$ed.Id];$tipText=if($exe){$exe}else{YYText ('这台电脑上没找到，点这里选择 '+$ed.Exe) ('Not found on this PC; click to select '+$ed.Exe)}
    $tip.SetToolTip($buttons['poster-'+$ed.Id],$tipText);$tip.SetToolTip($buttons['install-'+$ed.Id],$tipText)
   }
   $tip.SetToolTip($buttons['video'],$script:K033VideoUrl);$tip.SetToolTip($buttons['home'],$script:K033HomepageUrl)
   $ready=@(Get-033Editions|Where-Object {$state.Exe[$_.Id]})
   $form.AcceptButton=$(if($ready.Count -eq 1){$buttons['install-'+$ready[0].Id]}else{$null})
   $form.Invalidate()
  }
  foreach($id in @('dark','light','zh','en')){$buttons[$id].Add_Click({switch($this.Tag.Spec.Id){'dark'{$script:YYLight=$false}'light'{$script:YYLight=$true}'zh'{$script:YYEnglish=$false}'en'{$script:YYEnglish=$true}};try{Save-033Appearance}catch{$state.Failure=$_.Exception.Message}; & $refresh})}
  $choose={param([string]$Edition,[string]$Path)
   try{
    $picked=Resolve-033EditionPath $Path $Edition
    $state.Exe[$picked.Edition]=$picked.Exe
    if(@($state.Choices[$picked.Edition]) -notcontains $picked.Exe){$state.Choices[$picked.Edition]=@($state.Choices[$picked.Edition])+@($picked.Exe)}
    $script:YYProgress=$null
    if($picked.Moved){
     $errorBox.Text=$(if($picked.Edition -eq 'intl'){YYText '这是国际服的主程序（wwm.exe），已经放到右边「国际服」那张海报。' 'This is the international client (wwm.exe); it is now on the International poster on the right.'}else{YYText '这是国服的主程序（yysls.exe），已经放到左边「国服」那张海报。' 'This is the China client (yysls.exe); it is now on the China poster on the left.'})
     $errorBox.Visible=$true
    }else{$errorBox.Visible=$false}
   }catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh
  }
  $pickFile={param([string]$Edition)
   $ed=Get-033Edition $Edition
   if($script:K033LauncherPickFile){return (& $script:K033LauncherPickFile $Edition)}
   $dialog=[Windows.Forms.OpenFileDialog]::new()
   try{
    $dialog.Title=YYText ('选择燕云'+(Get-033ButtonText ('poster-'+$Edition))+'主程序 '+$ed.Exe) ('Select the '+(Get-033ButtonText ('poster-'+$Edition))+' client '+$ed.Exe)
    $dialog.Filter=(YYText '燕云国服 (yysls.exe)' 'YanYun China (yysls.exe)')+'|yysls.exe|'+(YYText '燕云国际服 Where Winds Meet (wwm.exe)' 'Where Winds Meet (wwm.exe)')+'|wwm.exe'
    $dialog.FilterIndex=$(if($Edition -eq 'intl'){2}else{1});$dialog.CheckFileExists=$true;$dialog.RestoreDirectory=$true
    if($state.Exe[$Edition]){$dialog.InitialDirectory=Split-Path -Parent $state.Exe[$Edition]}
    if($dialog.ShowDialog($form) -eq 'OK'){return $dialog.FileName};return $null
   }finally{$dialog.Dispose()}
  }
  $browse={param([string]$Edition,$Anchor)
   # More than one copy of this client found: offer them, then the file picker.
   $others=@($state.Choices[$Edition]|Where-Object {$_})
   if($others.Count -gt 1 -and -not $script:K033LauncherPickFile){
    if($state.Menu){$state.Menu.Dispose()};$menu=[Windows.Forms.ContextMenuStrip]::new();$state.Menu=$menu;$menu.Font=$uiFont
    foreach($choice in $others){$item=$menu.Items.Add($choice);$item.Tag=@{Edition=$Edition;Path=$choice};$item.Checked=($choice -eq $state.Exe[$Edition]);$item.Add_Click({& $choose $this.Tag.Edition $this.Tag.Path})}
    [void]$menu.Items.Add('-');$more=$menu.Items.Add((YYText '选择其他位置…' 'Choose another location…'));$more.Tag=@{Edition=$Edition};$more.Add_Click({$file=& $pickFile $this.Tag.Edition;if($file){& $choose $this.Tag.Edition $file}})
    $menu.Show($Anchor,[Drawing.Point]::new(0,$Anchor.Height));return
   }
   $file=& $pickFile $Edition;if($file){& $choose $Edition $file}
  }
  $run={param([string]$Edition,[string]$Action,[bool]$Clean)
   if($state.Busy){return}
   $exe=$state.Exe[$Edition]
   if(!$exe){
    if($Action -eq 'Restore' -or $Clean){$errorBox.Text=YYText ('先选择'+(Get-033ButtonText ('poster-'+$Edition))+'的游戏位置。') ('Choose the '+(Get-033ButtonText ('poster-'+$Edition))+' game location first.');$errorBox.Visible=$true;& $refresh;return}
    & $browse $Edition $buttons['browse-'+$Edition];return
   }
   try{
    [void](Get-033YanYunTargets @($exe));$state.Action=$Action;$state.Clean=$Clean;$state.Edition=$Edition
    if($Clean){Assert-033YanYunCleanSupported @($exe);$ask=YYText ('净化后安装：先把燕云两个入口目录（Win64r、Win64rh）里不属于游戏的文件——别的模组（ReShade、OptiScaler、ENB、Special K 等）、没有游戏厂商签名的程序文件、旧版残留——全部移进备份库，再装 033。'+"`r`n`r`n"+'游戏自己的文件和存档不动，033 的设置保留。点「还原安装前」可以把移走的东西原样放回。'+"`r`n`r`n"+'继续吗？') ('Clean install moves every file in the two YanYun binary folders that does not belong to the game (other mods, programs without a game-vendor signature, old leftovers) into the backup vault, then installs 033. Game files and saves stay; Restore original puts everything back.'+"`r`n`r`n"+'Continue?')
     $answer=if($script:K033LauncherConfirm){& $script:K033LauncherConfirm $ask}else{[string][Windows.Forms.MessageBox]::Show($form,$ask,(YYText '净化后安装' 'Clean install'),'YesNo','Question')}
     if([string]$answer -ne 'Yes'){return}}
    $jobs=Join-Path $env:LOCALAPPDATA ('033YanYunRuntime/installer-jobs/'+[Guid]::NewGuid().ToString('N'));[void][IO.Directory]::CreateDirectory($jobs)
    $state.Progress=Join-Path $jobs 'progress.json';$state.Log=Join-Path $jobs 'operation.log'
    $encoded=New-033WorkerCommand (Join-Path $PSScriptRoot 'dlss5_install.ps1') $exe $Action $state.Progress $state.Log -Clean:$Clean
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe';$info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand '+$encoded;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.WorkingDirectory=$PSScriptRoot
    $script:YYProgress=@{Stage='check';Done=0;Total=0;Detail=''}
    $state.Process=$(if($script:K033LauncherStartWorker){& $script:K033LauncherStartWorker $info}else{[Diagnostics.Process]::Start($info)});$state.Busy=$true;$errorBox.Visible=$false
   }catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh
  }
  foreach($ed in Get-033Editions){
   $buttons['poster-'+$ed.Id].Add_Click({& $run $this.Tag.Spec.Edition 'Install' $false})
   $buttons['install-'+$ed.Id].Add_Click({& $run $this.Tag.Spec.Edition 'Install' $false})
   $buttons['restore-'+$ed.Id].Add_Click({& $run $this.Tag.Spec.Edition 'Restore' $false})
   $buttons['browse-'+$ed.Id].Add_Click({if(!$state.Busy){& $browse $this.Tag.Spec.Edition $this}})
   if($ed.Clean){$buttons['clean-'+$ed.Id].Add_Click({& $run $this.Tag.Spec.Edition 'Install' $true})}
  }
  $buttons.minimize.Add_Click({$form.WindowState='Minimized'});$buttons.close.Add_Click({if(!$state.Busy){$form.Close()}})
  # A browser that does not open leaves the address in the window, to copy.
  $buttons.video.Add_Click({try{Open-033Url $script:K033VideoUrl}catch{$errorBox.Text=(YYText '没能打开浏览器，下载页面：' 'Could not open the browser. Download page: ')+$script:K033VideoUrl;$errorBox.Visible=$true}})
  $buttons.home.Add_Click({try{Open-033Url $script:K033HomepageUrl}catch{$errorBox.Text=(YYText '没能打开浏览器，作者主页：' 'Could not open the browser. Author page: ')+$script:K033HomepageUrl;$errorBox.Visible=$true}})
  $timer=[Windows.Forms.Timer]::new();$timer.Interval=200
  $timer.Add_Tick({
   if(!$state.Busy -or !$state.Process){return}
   try{if(Test-Path -LiteralPath $state.Progress){$script:YYProgress=[IO.File]::ReadAllText($state.Progress)|ConvertFrom-Json}}catch{}
   if($state.Process.HasExited){
    $exitCode=$state.Process.ExitCode;$state.Process.Dispose();$state.Process=$null;$state.Busy=$false;$result=$null
    try{$result=[IO.File]::ReadAllText(($state.Progress+'.result.json'))|ConvertFrom-Json}catch{}
    if($exitCode -eq 0 -and $result -and $result.Succeeded){$cleanedCount=0;try{if($result.Result -and $result.Result.PSObject.Properties['Cleaned']){$cleanedCount=@($result.Result.Cleaned).Count}}catch{$cleanedCount=0};$script:YYProgress=$(if($cleanedCount){@{Stage='complete';Done=0;Total=0;Detail=(YYText ('已把 '+$cleanedCount+' 个不属于游戏的文件移进备份库（「还原安装前」可放回）') ('Moved '+$cleanedCount+' non-game files to the backup vault (Restore original puts them back)'))}}else{@{Stage='complete';Done=1;Total=1;Detail=''}});$errorBox.Visible=$false}
    else{$script:YYProgress=@{Stage='failed';Done=0;Total=0;Detail=''};$errorBox.Text=$(if($result){$result.Message}else{YYText '安装未完成，请查看日志。' 'Installation did not complete. See the log.'})+"`r`n"+$state.Log;$errorBox.Visible=$true}
    & $refresh
   };$form.Invalidate()
  });$timer.Start()
  # Each client goes on its own card; a path given on the command line goes on its card first.
  $discover={
   $split=Split-033YanYunDetections @(Find-033YanYunInstallations (Get-033YanYunLocalSeeds $PSScriptRoot))
   foreach($ed in Get-033Editions){$state.Choices[$ed.Id]=@($split[$ed.Id])}
   if($PreExe){$given=Get-033EditionOf $PreExe;if(-not $given -and (Test-Path -LiteralPath $PreExe -PathType Container)){$given='cn'};if($given){& $choose $given $PreExe}}
   foreach($ed in Get-033Editions){if(-not $state.Exe[$ed.Id] -and @($state.Choices[$ed.Id]).Count){& $choose $ed.Id @($state.Choices[$ed.Id])[0]}}
  }
  & $refresh
  # Show the window before bounded local discovery, so there is no console-only wait.
  $form.Add_Shown({
   $form.BeginInvoke([Action]{try{& $discover}catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh})|Out-Null
  })
  if($Exercise){& $Exercise}else{[void]$form.ShowDialog()}
  return (Get-033LauncherAction 'exit')
 }catch{
  if($Exercise){throw}
  try{[void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'033',0,16)}catch{}
  return (Get-033LauncherAction 'exit')
 }finally{if($timer){$timer.Stop();$timer.Dispose()};if($state -and $state.Menu){$state.Menu.Dispose()};if($tip){$tip.Dispose()};if($uiFont){$uiFont.Dispose()};if($errorFont){$errorFont.Dispose()};if($form){$form.Dispose()}}
}
