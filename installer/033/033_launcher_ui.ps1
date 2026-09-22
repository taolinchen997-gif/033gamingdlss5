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
function Get-033LauncherLayout {
 return @{Width=740;Height=740;Header=326;Path=@(40,394,476,52);PathText=@(52,400,448,40);Buttons=@(
  @{Id='browse';Text='更改位置';Bounds=@(534,394,166,52);Style='outline';Tab=1},
  @{Id='install';Text='安装 / 升级';Bounds=@(76,537,588,71);Style='primary';Tab=2},
  @{Id='restore';Text='还原安装前';Bounds=@(106,620,254,38);Style='link';Tab=3},
  @{Id='clean';Text='净化后安装';Bounds=@(380,620,254,38);Style='link';Tab=10},
  @{Id='dark';Text='黑';Bounds=@(386,14,56,29);Style='choice';Tab=4},
  @{Id='light';Text='白';Bounds=@(448,14,56,29);Style='choice';Tab=5},
  @{Id='zh';Text='中';Bounds=@(520,14,50,29);Style='choice';Tab=6},
  @{Id='en';Text='EN';Bounds=@(576,14,50,29);Style='choice';Tab=7},
  @{Id='minimize';Text='最小化';Bounds=@(646,14,28,29);Style='system';Tab=8},
  @{Id='close';Text='关闭';Bounds=@(694,14,28,29);Style='system';Tab=9})}
}
function Get-033ButtonText([string]$Id){switch($Id){
 'browse'{YYText '更改位置' 'Change'}'install'{YYText '安装 / 升级' 'Install / Update'}'restore'{YYText '还原安装前' 'Restore original'}'clean'{YYText '净化后安装' 'Clean install'}
 'dark'{YYText '黑' 'Dark'}'light'{YYText '白' 'Light'}'zh'{'中'}'en'{'EN'}'minimize'{YYText '最小化' 'Minimize'}'close'{YYText '关闭' 'Close'}
}}
function New-033RoundPath($Bounds,[float]$Radius=9){
 $x=[float]$Bounds[0];$y=[float]$Bounds[1];$w=[float]$Bounds[2];$h=[float]$Bounds[3];$d=[Math]::Min(2*$Radius,[Math]::Min($w,$h))
 $path=[Drawing.Drawing2D.GraphicsPath]::new();$path.AddArc($x,$y,$d,$d,180,90);$path.AddArc(($x+$w-$d),$y,$d,$d,270,90);$path.AddArc(($x+$w-$d),($y+$h-$d),$d,$d,0,90);$path.AddArc($x,($y+$h-$d),$d,$d,90,90);$path.CloseFigure();return $path
}
function Draw-033LauncherButton($Graphics,$Spec,$Bounds,[bool]$Hover=$false,[bool]$Focused=$false){
 $c=Get-033Palette;$x=[float]$Bounds[0];$y=[float]$Bounds[1];$w=[float]$Bounds[2];$h=[float]$Bounds[3]
 $buttonBack=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Card));try{$Graphics.FillRectangle($buttonBack,$x,$y,$w,$h)}finally{$buttonBack.Dispose()};$edge=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($(if($Hover -or $Focused){$c.Gold}else{$c.Edge})),1)
 $path=New-033RoundPath @(($x+1),($y+1),($w-2),($h-2)) 8;$fill=$null
 try{
  if($Spec.Style -eq 'system'){
   $edge.Color=[Drawing.ColorTranslator]::FromHtml($c.Text)
   if($Spec.Id -eq 'close'){$Graphics.DrawLine($edge,($x+8),($y+8),($x+$w-8),($y+$h-8));$Graphics.DrawLine($edge,($x+$w-8),($y+8),($x+8),($y+$h-8))}
   else{$Graphics.DrawLine($edge,($x+8),($y+$h/2),($x+$w-8),($y+$h/2))};return
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
function Draw-033Launcher($Graphics,$Layout,[string]$Exe,[string]$Status,[bool]$IncludeControls=$false){
 $c=Get-033Palette;$Graphics.SmoothingMode='AntiAlias';$Graphics.TextRenderingHint='AntiAliasGridFit';$Graphics.Clear([Drawing.ColorTranslator]::FromHtml($c.Bg))
 $edge=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($c.Edge),1);$card=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Card));$outer=New-033RoundPath @(1,1,738,738) 12;$box=New-033RoundPath @(18,327,704,348) 10
 try{
  if(!$script:YYArtwork){$script:YYArtwork=[Drawing.Image]::FromFile($script:YYArtworkPath)}
  # Draw the approved artwork itself, selected by source rectangle. No new illustration.
  $sourceX=if($script:YYLight){781}else{29}
  $Graphics.DrawImage($script:YYArtwork,[Drawing.Rectangle]::new(0,64,740,263),$sourceX,185,724,262,[Drawing.GraphicsUnit]::Pixel)
  # Keep the original banner and title pixels when they match the active language.
  # For the other two combinations only the static title band is localized.
  if($script:YYLight -ne $script:YYEnglish){
   $base=[Drawing.ColorTranslator]::FromHtml($c.Bg);$titleFill=[Drawing.SolidBrush]::new($base)
   try{$Graphics.FillRectangle($titleFill,0,230,455,76)}finally{$titleFill.Dispose()}
   Draw-033Text $Graphics (YYText '燕云定制版安装器' 'YANYUN INSTALLER') 32 $c.Text @(40,230,410,58) $false $true
  }
  $saved=$Graphics.Save();try{$Graphics.TranslateTransform(2,6);Draw-033Brand $Graphics}finally{$Graphics.Restore($saved)}
  Draw-033Text $Graphics (YYText '燕云定制版' 'YANYUN EDITION') 18 $c.Text @(115,16,260,34) $false $true
  $Graphics.FillPath($card,$box);$Graphics.DrawPath($edge,$box);$pathFrame=New-033RoundPath $Layout.Path 6;try{$Graphics.DrawPath($edge,$pathFrame)}finally{$pathFrame.Dispose()}
  Draw-033Text $Graphics (YYText '游戏位置' 'Game location') 22 $c.Text @(42,348,190,31) $false $true
  Draw-033Text $Graphics $(if($Exe){YYText '已识别燕云' 'YanYun detected'}else{YYText '请选择燕云位置' 'Select game location'}) 15 $c.Gold @(240,348,420,31)
  $stage=if($script:YYProgress){Get-033StageText $script:YYProgress.Stage}else{YYText '安装前请退出游戏' 'Close the game before installing'}
  Draw-033Text $Graphics $stage 17 $c.Text @(36,475,668,30) $true
  if($script:YYProgress){
   $bar=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Track));$fill=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($c.Gold))
   try{$Graphics.FillRectangle($bar,76,517,588,7);$value=Get-033ProgressValue $script:YYProgress;if($value -ge 0){$Graphics.FillRectangle($fill,76,517,([float](588*$value/100)),7)}else{$Graphics.FillRectangle($fill,76,517,120,7)}}finally{$bar.Dispose();$fill.Dispose()}
   $detail=if($script:YYProgress.Total -gt 0){[string]$script:YYProgress.Done+' / '+$script:YYProgress.Total}else{[string]$script:YYProgress.Detail}
   Draw-033Text $Graphics $detail 12 $c.Muted @(38,497,664,18) $true
  }
  Draw-033Text $Graphics (YYText '安装时自动备份' 'Backed up automatically') 17 $c.Muted @(38,695,300,25)
  Draw-033Text $Graphics (YYText '作者 B站@热心网友033' 'Author Bilibili @热心网友033') 17 $c.Text @(366,695,342,25) $true
  $Graphics.DrawPath($edge,$outer)
  if($IncludeControls){
   Draw-033Text $Graphics $(if($Exe){$Exe}else{YYText '选择 yysls.exe' 'Select yysls.exe'}) 15 $c.Text $Layout.PathText
   foreach($spec in $Layout.Buttons){$saved=$Graphics.Save();try{$b=$spec.Bounds;$Graphics.SetClip([Drawing.RectangleF]::new($b[0],$b[1],$b[2],$b[3]));Draw-033LauncherButton $Graphics $spec $b}finally{$Graphics.Restore($saved)}}
  }
 }finally{$card.Dispose();$edge.Dispose();$outer.Dispose();$box.Dispose()}
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
function Show-Launcher {
 param([string]$PreExe='')
 $form=$null;$timer=$null;$tip=$null;$uiFont=$null;$errorFont=$null
 try{
  Enable-DpiAware;Add-Type -AssemblyName System.Windows.Forms;Add-Type -AssemblyName System.Drawing;Read-033Appearance
  $layout=Get-033LauncherLayout
  $dpi=[Drawing.Graphics]::FromHwnd([IntPtr]::Zero);try{$scale=$dpi.DpiX/96.0}finally{$dpi.Dispose()}
  $workArea=[Windows.Forms.Screen]::PrimaryScreen.WorkingArea
  $scale=[Math]::Min($scale,[Math]::Min(($workArea.Height-24)/$layout.Height,($workArea.Width-24)/$layout.Width))
  function U([double]$Value){return [int][Math]::Round($Value*$scale)}
  $state=@{Exe=$PreExe;Busy=$false;Process=$null;Progress='';Log='';Action='';Clean=$false;Drag=$false;DragPoint=$null;Failure=''}
  $form=[Windows.Forms.Form]::new();$form.Text='033 燕云定制版';$form.FormBorderStyle='None';$form.AutoScaleMode='None';$form.StartPosition='CenterScreen';$form.KeyPreview=$true
  $form.ClientSize=[Drawing.Size]::new((U $layout.Width),(U $layout.Height))
  $form.GetType().GetProperty('DoubleBuffered',[Reflection.BindingFlags]'Instance,NonPublic').SetValue($form,$true,$null)
  $form.Add_Paint({$save=$_.Graphics.Save();try{$_.Graphics.ScaleTransform($scale,$scale);Draw-033Launcher $_.Graphics $layout $state.Exe ''}finally{$_.Graphics.Restore($save)}})
  $form.Add_MouseDown({if($_.Button -eq 'Left' -and $_.Y -lt (U 54)){$state.Drag=$true;$state.DragPoint=$_.Location;$form.Capture=$true}})
  $form.Add_MouseMove({if($state.Drag){$form.Location=[Drawing.Point]::new(($form.Left+$_.X-$state.DragPoint.X),($form.Top+$_.Y-$state.DragPoint.Y))}})
  $form.Add_MouseUp({$state.Drag=$false;$form.Capture=$false});$form.Add_MouseCaptureChanged({if(!$form.Capture){$state.Drag=$false}})
  $form.Add_FormClosing({if($state.Busy){$_.Cancel=$true}})
  $form.Add_KeyDown({if($_.KeyCode -eq 'Escape' -and !$state.Busy){$form.Close()}})
  $tip=[Windows.Forms.ToolTip]::new();$uiFont=[Drawing.Font]::new('Microsoft YaHei UI',(15*$scale),[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
  $errorFont=[Drawing.Font]::new('Microsoft YaHei UI',(13*$scale),[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
  $pathBox=[Windows.Forms.ComboBox]::new();$p=$layout.PathText;$pathBox.SetBounds((U $p[0]),(U $p[1]),(U $p[2]),(U $p[3]));$pathBox.DropDownStyle='DropDownList';$pathBox.FlatStyle='Flat';$pathBox.Font=$uiFont;$pathBox.TabIndex=0;$pathBox.DropDownWidth=(U 680);$form.Controls.Add($pathBox)
  $errorBox=[Windows.Forms.TextBox]::new();$errorBox.SetBounds((U 36),(U 462),(U 668),(U 67));$errorBox.Font=$errorFont;$errorBox.Multiline=$true;$errorBox.ReadOnly=$true;$errorBox.ScrollBars='Vertical';$errorBox.BorderStyle='None';$errorBox.Visible=$false;$form.Controls.Add($errorBox)
  $buttons=@{}
  foreach($spec in $layout.Buttons){
   $button=[Windows.Forms.Button]::new();$button.Tag=@{Spec=$spec;Hover=$false};$p=$spec.Bounds;$button.SetBounds((U $p[0]),(U $p[1]),(U $p[2]),(U $p[3]));$button.FlatStyle='Flat';$button.FlatAppearance.BorderSize=0;$button.TabIndex=$spec.Tab;$button.Font=$uiFont;$button.Cursor=[Windows.Forms.Cursors]::Hand
   $button.Add_Paint({$save=$_.Graphics.Save();try{$_.Graphics.ScaleTransform($scale,$scale);$_.Graphics.SmoothingMode='AntiAlias';$_.Graphics.TextRenderingHint='AntiAliasGridFit';Draw-033LauncherButton $_.Graphics $this.Tag.Spec @(0,0,($this.Width/$scale),($this.Height/$scale)) $this.Tag.Hover $this.Focused}finally{$_.Graphics.Restore($save)}})
   $button.Add_MouseEnter({$this.Tag.Hover=$true;$this.Invalidate()});$button.Add_MouseLeave({$this.Tag.Hover=$false;$this.Invalidate()});$button.Add_GotFocus({$this.Invalidate()});$button.Add_LostFocus({$this.Invalidate()})
   $buttons[$spec.Id]=$button;$form.Controls.Add($button)
  }
  $refresh={
   $colors=Get-033Palette;$form.BackColor=[Drawing.ColorTranslator]::FromHtml($colors.Bg);$pathBox.BackColor=[Drawing.ColorTranslator]::FromHtml($colors.Card);$pathBox.ForeColor=[Drawing.ColorTranslator]::FromHtml($colors.Text)
   $pathBox.AccessibleName=YYText '游戏位置' 'Game location';$errorBox.BackColor=$pathBox.BackColor;$errorBox.ForeColor=$pathBox.ForeColor
   foreach($id in $buttons.Keys){$b=$buttons[$id];$b.Text=Get-033ButtonText $id;$b.AccessibleName=$b.Text;$b.BackColor=$form.BackColor;$b.Invalidate()}
   foreach($id in @('browse','install','restore','clean')){$buttons[$id].Enabled=!$state.Busy};$pathBox.Enabled=!$state.Busy;$buttons.close.Enabled=!$state.Busy
   $form.Invalidate()
  }
  foreach($id in @('dark','light','zh','en')){$buttons[$id].Add_Click({switch($this.Tag.Spec.Id){'dark'{$script:YYLight=$false}'light'{$script:YYLight=$true}'zh'{$script:YYEnglish=$false}'en'{$script:YYEnglish=$true}};try{Save-033Appearance}catch{$state.Failure=$_.Exception.Message}; & $refresh})}
  $choose={param([string]$Path)
   try{
    if(Test-Path -LiteralPath $Path -PathType Container){$found=@(Find-033YanYunInstallations @($Path));if($found.Count -ne 1){throw (YYText '请选择准确的 yysls.exe' 'Select the exact yysls.exe')};$Path=$found[0]}
    [void](Get-033YanYunTargets @($Path));$state.Exe=[IO.Path]::GetFullPath($Path)
    if(!$pathBox.Items.Contains($state.Exe)){[void]$pathBox.Items.Add($state.Exe)};$pathBox.SelectedItem=$state.Exe;$tip.SetToolTip($pathBox,$state.Exe);$errorBox.Visible=$false;$script:YYProgress=$null
   }catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh
  }
  $pathBox.Add_SelectedIndexChanged({if($pathBox.SelectedIndex -ge 0){$state.Exe=[string]$pathBox.SelectedItem;$tip.SetToolTip($pathBox,$state.Exe);$form.Invalidate()}})
  $buttons.browse.Add_Click({$dialog=[Windows.Forms.OpenFileDialog]::new();try{$dialog.Title=YYText '选择燕云主程序 yysls.exe' 'Select YanYun yysls.exe';$dialog.Filter='YanYun (yysls.exe)|yysls.exe';$dialog.CheckFileExists=$true;$dialog.RestoreDirectory=$true;if($state.Exe){$dialog.InitialDirectory=Split-Path -Parent $state.Exe};if($dialog.ShowDialog($form) -eq 'OK'){& $choose $dialog.FileName}}finally{$dialog.Dispose()}})
  foreach($id in @('install','restore','clean')){$buttons[$id].Add_Click({
   if($state.Busy){return};if(!$state.Exe){$errorBox.Text=YYText '请先选择燕云位置。' 'Select the game location first.';$errorBox.Visible=$true;return}
   try{
    [void](Get-033YanYunTargets @($state.Exe));$state.Action=if($this.Tag.Spec.Id -eq 'restore'){'Restore'}else{'Install'};$state.Clean=($this.Tag.Spec.Id -eq 'clean')
    if($state.Clean){$ask=YYText ('净化后安装：先把燕云两个入口目录（Win64r、Win64rh）里不属于游戏的文件——别的模组（ReShade、OptiScaler、ENB、Special K 等）、没有游戏厂商签名的程序文件、旧版残留——全部移进备份库，再装 033。'+"`r`n`r`n"+'游戏自己的文件和存档不动，033 的设置保留。点「还原安装前」可以把移走的东西原样放回。'+"`r`n`r`n"+'继续吗？') ('Clean install moves every file in the two YanYun binary folders that does not belong to the game (other mods, programs without a game-vendor signature, old leftovers) into the backup vault, then installs 033. Game files and saves stay; Restore original puts everything back.'+"`r`n`r`n"+'Continue?')
     if([string][Windows.Forms.MessageBox]::Show($form,$ask,(YYText '净化后安装' 'Clean install'),'YesNo','Question') -ne 'Yes'){return}}
    $jobs=Join-Path $env:LOCALAPPDATA ('033YanYunRuntime/installer-jobs/'+[Guid]::NewGuid().ToString('N'));[void][IO.Directory]::CreateDirectory($jobs)
    $state.Progress=Join-Path $jobs 'progress.json';$state.Log=Join-Path $jobs 'operation.log'
    $encoded=New-033WorkerCommand (Join-Path $PSScriptRoot 'dlss5_install.ps1') $state.Exe $state.Action $state.Progress $state.Log -Clean:([bool]$state.Clean)
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe';$info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand '+$encoded;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.WorkingDirectory=$PSScriptRoot
    $script:YYProgress=@{Stage='check';Done=0;Total=0;Detail=''};$state.Process=[Diagnostics.Process]::Start($info);$state.Busy=$true;$errorBox.Visible=$false
   }catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh
  })}
  $buttons.minimize.Add_Click({$form.WindowState='Minimized'});$buttons.close.Add_Click({if(!$state.Busy){$form.Close()}})
  $form.AcceptButton=$buttons.install
  $timer=[Windows.Forms.Timer]::new();$timer.Interval=200
  $timer.Add_Tick({
   if(!$state.Busy){return}
   try{if(Test-Path -LiteralPath $state.Progress){$script:YYProgress=[IO.File]::ReadAllText($state.Progress)|ConvertFrom-Json}}catch{}
   if($state.Process.HasExited){
    $exitCode=$state.Process.ExitCode;$state.Process.Dispose();$state.Process=$null;$state.Busy=$false;$result=$null
    try{$result=[IO.File]::ReadAllText(($state.Progress+'.result.json'))|ConvertFrom-Json}catch{}
    if($exitCode -eq 0 -and $result -and $result.Succeeded){$cleanedCount=0;try{if($result.Result -and $result.Result.PSObject.Properties['Cleaned']){$cleanedCount=@($result.Result.Cleaned).Count}}catch{$cleanedCount=0};$script:YYProgress=$(if($cleanedCount){@{Stage='complete';Done=0;Total=0;Detail=(YYText ('已把 '+$cleanedCount+' 个不属于游戏的文件移进备份库（「还原安装前」可放回）') ('Moved '+$cleanedCount+' non-game files to the backup vault (Restore original puts them back)'))}}else{@{Stage='complete';Done=1;Total=1;Detail=''}});$errorBox.Visible=$false}
    else{$script:YYProgress=@{Stage='failed';Done=0;Total=0;Detail=''};$errorBox.Text=$(if($result){$result.Message}else{YYText '安装未完成，请查看日志。' 'Installation did not complete. See the log.'})+"`r`n"+$state.Log;$errorBox.Visible=$true}
    & $refresh
   };$form.Invalidate()
  });$timer.Start()
  & $refresh
  # Show the window before bounded local discovery, so there is no console-only wait.
  $form.Add_Shown({
   $form.BeginInvoke([Action]{try{$detected=@(Find-033YanYunInstallations (Get-033YanYunLocalSeeds $PSScriptRoot));$selection=Get-033YanYunLaunchSelection $PreExe $detected;foreach($choice in $selection.Choices){if(!$pathBox.Items.Contains($choice)){[void]$pathBox.Items.Add($choice)}};if($selection.Exe){& $choose $selection.Exe}}catch{$errorBox.Text=$_.Exception.Message;$errorBox.Visible=$true}; & $refresh})|Out-Null
  })
  [void]$form.ShowDialog();return (Get-033LauncherAction 'exit')
 }catch{
  try{[void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'033',0,16)}catch{}
  return (Get-033LauncherAction 'exit')
 }finally{if($timer){$timer.Stop();$timer.Dispose()};if($tip){$tip.Dispose()};if($uiFont){$uiFont.Dispose()};if($errorFont){$errorFont.Dispose()};if($form){$form.Dispose()}}
}
