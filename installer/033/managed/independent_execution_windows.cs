// A held, exclusive DATA-write-capable handle excludes an already mapped
// executable image and new opens for execution. The handle is NEVER written.
// Read-only target leases and process-name snapshots cannot provide this gate.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace Installer033 {
    // Paired with runtime NativePreferences' lifetime shared DATA handle.
    // This is session exclusion, distinct from the existing one-byte save lock.
    public sealed class SettingsSessionGateV1 : IDisposable {
        private ExecutionTargetLeaseV1.DirectoryScopeLease scope;
        public SettingsSessionGateV1(string settingsRoot) {
            try{scope=ExecutionTargetLeaseV1.CreateAndPinDirectoryRoot(settingsRoot,"settings.sessions.v1.lock",true);}
            catch(Win32Exception e){throw new Win32Exception(e.NativeErrorCode,"SharedSettingsSessionsBusy: cannot exclude all participating 033 sessions: "+e.Message);}
        }
        public void AssertHeld(){if(scope==null)throw new ObjectDisposedException("SettingsSessionGateV1");scope.AssertHeld();}
        public void Dispose(){if(scope!=null){scope.Dispose();scope=null;}}
    }
    public sealed class ExecutionTargetLeaseV1 : IDisposable {
        [StructLayout(LayoutKind.Sequential)] private struct Info {
            public uint Attributes,CreationLow,CreationHigh,AccessLow,AccessHigh,WriteLow,WriteHigh,Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
        }
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        private static extern SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
        [DllImport("kernel32.dll",SetLastError=true)] private static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info info);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        private static extern uint GetFinalPathNameByHandle(SafeFileHandle handle,StringBuilder path,uint length,uint flags);
        private readonly List<SafeFileHandle> directories=new List<SafeFileHandle>();
        private readonly List<string> directoryPaths=new List<string>();
        private readonly List<string> directoryIdentities=new List<string>();
        private FileStream stream;
        public readonly string Path,Directory,FileIdentity,DirectoryIdentity,SHA256,LastWriteUtc;
        public readonly long Bytes;
        public readonly uint Attributes;
        private static Info Information(SafeFileHandle handle) {
            Info info;if(handle.IsInvalid||!GetFileInformationByHandle(handle,out info))throw new Win32Exception(Marshal.GetLastWin32Error());return info;
        }
        private static string Identity(Info info){return info.Volume.ToString("x8")+":"+info.IndexHigh.ToString("x8")+info.IndexLow.ToString("x8");}
        private static string Final(SafeFileHandle handle) {
            var result=new StringBuilder(32768);uint count=GetFinalPathNameByHandle(handle,result,32768,0);
            if(count==0||count>=32768)throw new Win32Exception(Marshal.GetLastWin32Error());
            string path=result.ToString();if(path.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase))throw new IOException("Remote execution targets are unsupported");
            if(path.StartsWith(@"\\?\",StringComparison.Ordinal))path=path.Substring(4);return path.Length>3?path.TrimEnd('\\'):path;
        }
        private string Hash() {
            stream.Position=0;using(var hash=System.Security.Cryptography.SHA256.Create())return BitConverter.ToString(hash.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
        }
        public sealed class DirectoryScopeLease : IDisposable {
            // ABI checked against SDK 10.0.26100 winternl.h: UNICODE_STRING,
            // OBJECT_ATTRIBUTES, IO_STATUS_BLOCK, NtCreateFile and constants.
            [StructLayout(LayoutKind.Sequential)] private struct UnicodeString {public ushort Length,MaximumLength;public IntPtr Buffer;}
            [StructLayout(LayoutKind.Sequential)] private struct ObjectAttributes {public uint Length;public IntPtr RootDirectory,ObjectName;public uint Attributes;public IntPtr SecurityDescriptor,SecurityQualityOfService;}
            [StructLayout(LayoutKind.Sequential)] private struct IoStatusBlock {public IntPtr Status;public UIntPtr Information;}
            [DllImport("ntdll.dll")] private static extern int NtCreateFile(out SafeFileHandle handle,uint access,ref ObjectAttributes attributes,out IoStatusBlock status,IntPtr allocation,uint fileAttributes,uint share,uint disposition,uint options,IntPtr ea,uint eaLength);
            [DllImport("ntdll.dll")] private static extern uint RtlNtStatusToDosError(int status);
            private readonly List<SafeFileHandle> handles=new List<SafeFileHandle>();
            private readonly List<string> paths=new List<string>(),identities=new List<string>();
            private SafeFileHandle anchor;
            private string anchorPath,anchorIdentity;
            private uint anchorAttributes;
            private static SafeFileHandle Relative(SafeFileHandle parent,string name,uint access,uint share,uint disposition,uint options,uint attributes){
                if(String.IsNullOrEmpty(name)||name.Length>255||name.IndexOfAny(new char[]{'\\','/',':','\0'})>=0||name=="."||name=="..")throw new IOException("Use one literal relative component");
                IntPtr buffer=IntPtr.Zero,unicode=IntPtr.Zero;bool retained=false;SafeFileHandle child=null;
                try{
                    buffer=Marshal.StringToHGlobalUni(name);
                    var text=new UnicodeString{Length=(ushort)(name.Length*2),MaximumLength=(ushort)((name.Length+1)*2),Buffer=buffer};
                    unicode=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(UnicodeString)));Marshal.StructureToPtr(text,unicode,false);
                    parent.DangerousAddRef(ref retained);
                    var objectAttributes=new ObjectAttributes{Length=(uint)Marshal.SizeOf(typeof(ObjectAttributes)),RootDirectory=parent.DangerousGetHandle(),ObjectName=unicode,Attributes=0x1040}; // CASE_INSENSITIVE | DONT_REPARSE
                    IoStatusBlock io;int status=NtCreateFile(out child,access,ref objectAttributes,out io,IntPtr.Zero,attributes,share,disposition,options,IntPtr.Zero,0);
                    if(status<0)throw new Win32Exception((int)RtlNtStatusToDosError(status),"Relative directory/anchor open rejected, NTSTATUS 0x"+unchecked((uint)status).ToString("x8"));
                    ulong resultKind=io.Information.ToUInt64();
                    bool expectedKind=(disposition==1&&resultKind==1)||(disposition==2&&resultKind==2)||(disposition==3&&(resultKind==1||resultKind==2));
                    if(status!=0||!expectedKind||child==null||child.IsInvalid)throw new IOException("Unexpected relative-open completion, disposition or handle");
                    SafeFileHandle result=child;child=null;return result;
                }finally{if(child!=null)child.Dispose();if(retained)parent.DangerousRelease();if(unicode!=IntPtr.Zero)Marshal.FreeHGlobal(unicode);if(buffer!=IntPtr.Zero)Marshal.FreeHGlobal(buffer);}
            }
            private void AddDirectory(SafeFileHandle handle,string path){
                handles.Add(handle);Info info=Information(handle);
                if((info.Attributes&0x400)!=0||(info.Attributes&0x10)==0||!String.Equals(Final(handle),path,StringComparison.OrdinalIgnoreCase))throw new IOException("Execution directory is indirect or moved before anchoring");
                paths.Add(path);identities.Add(Identity(info));
            }
            internal DirectoryScopeLease(string root,string guardName,bool persistent,bool createNewLeaf) {
                try {
                    string full=System.IO.Path.GetFullPath(root).TrimEnd('\\');
                    if(full!=root||full.Length<4||full[1]!=':'||full[2]!='\\'||full.IndexOf(':',2)>=0)throw new IOException("Use one canonical local anchored directory");
                    if((persistent&&guardName!="settings.sessions.v1.lock"&&guardName!=".033-vault.lock")||(!persistent&&guardName!=".033-directory.guard"))throw new IOException("Unknown execution anchor contract");
                    string cursor=full.Substring(0,3);
                    AddDirectory(CreateFile(cursor,0x81,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero),cursor);
                    string[] components=full.Substring(3).Split('\\');
                    for(int i=0;i<components.Length;i++){
                        string component=components[i];
                        // Every creation is relative to the already held parent.
                        // No OPEN_REPARSE_POINT: OBJ_DONT_REPARSE rejects it.
                        var child=Relative(handles[handles.Count-1],component,0x100081,3,(createNewLeaf&&i==components.Length-1)?2u:3u,0x21,0x10);
                        cursor=System.IO.Path.Combine(cursor,component);AddDirectory(child,cursor);
                    }
                    anchorPath=System.IO.Path.Combine(full,guardName);
                    // Persistent session/vault gates use OPEN_IF. Temporary
                    // action guards use CREATE+DELETE_ON_CLOSE; never overwrite
                    // an existing filename and never permit deletion/rename.
                    anchor=Relative(handles[handles.Count-1],guardName,persistent?0xc0100000u:0xc0110000u,0,persistent?3u:2u,persistent?0x60u:0x1060u,2);
                    Info info=Information(anchor);
                    if((info.Attributes&(0x400|0x10))!=0||info.Links!=1||info.SizeHigh!=0||info.SizeLow!=0||!String.Equals(Final(anchor),anchorPath,StringComparison.OrdinalIgnoreCase))throw new IOException("Execution anchor is indirect, nonempty, hard-linked, or moved");
                    anchorIdentity=Identity(info);anchorAttributes=info.Attributes;AssertHeld();
                }catch{Dispose();throw;}
            }
            public void AssertHeld(){
                if(handles.Count==0||anchor==null)throw new ObjectDisposedException("DirectoryScopeLease");
                for(int i=0;i<handles.Count;i++){
                    Info info=Information(handles[i]);
                    if((info.Attributes&0x400)!=0||Identity(info)!=identities[i]||!String.Equals(Final(handles[i]),paths[i],StringComparison.OrdinalIgnoreCase))throw new IOException("Execution directory scope changed");
                }
                Info file=Information(anchor);
                if(file.Attributes!=anchorAttributes||file.Links!=1||file.SizeHigh!=0||file.SizeLow!=0||Identity(file)!=anchorIdentity||!String.Equals(Final(anchor),anchorPath,StringComparison.OrdinalIgnoreCase))throw new IOException("Execution anchor changed");
            }
            public void Dispose(){if(anchor!=null){anchor.Dispose();anchor=null;}for(int i=handles.Count-1;i>=0;i--)handles[i].Dispose();handles.Clear();}
        }
        public static DirectoryScopeLease CreateAndPinDirectoryRoot(string root,string guardName,bool persistent){return new DirectoryScopeLease(root,guardName,persistent,false);}
        public static DirectoryScopeLease CreateAndPinNewDirectoryRoot(string root){return new DirectoryScopeLease(root,".033-directory.guard",false,true);}
        public ExecutionTargetLeaseV1(string path) {
            try {
                Path=System.IO.Path.GetFullPath(path);Directory=System.IO.Path.GetDirectoryName(Path);
                if(Path!=path||Path.Length<4||Path[1]!=':'||Path[2]!='\\'||Path.IndexOf(':',2)>=0)throw new IOException("Execution target must be one canonical local path");
                var ancestors=new List<string>();string cursor=Directory;
                while(!String.IsNullOrEmpty(cursor)){ancestors.Add(cursor);string parent=System.IO.Path.GetDirectoryName(cursor);if(parent==cursor)break;cursor=parent;}
                ancestors.Reverse();
                foreach(string folder in ancestors){
                    // FileRenameInformation opens its destination directory
                    // with FILE_ADD_FILE. Share read/write so our atomic file
                    // moves can proceed; keep NO delete sharing on the directory.
                    var directory=CreateFile(folder,0x81,3,IntPtr.Zero,3,0x02000000|0x00200000,IntPtr.Zero);directories.Add(directory);
                    Info info=Information(directory);
                    if((info.Attributes&0x400)!=0||(info.Attributes&0x10)==0||!String.Equals(Final(directory),folder,StringComparison.OrdinalIgnoreCase))throw new IOException("Execution directory is indirect or changed");
                    directoryPaths.Add(folder);directoryIdentities.Add(Identity(info));DirectoryIdentity=Identity(info);
                }
                // GENERIC_READ | GENERIC_WRITE, NO sharing. OPEN_EXISTING only.
                // No ACL/attribute change and no fallback to a read-only open.
                var file=CreateFile(Path,0xc0000000,0,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
                try {
                    if(file.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"TargetBusyOrNotWritable: exclusive executable lease unavailable");
                    Info info=Information(file);
                    if((info.Attributes&(0x400|0x10))!=0||info.Links!=1||!String.Equals(Final(file),Path,StringComparison.OrdinalIgnoreCase))throw new IOException("Execution target is indirect or hard-linked");
                    stream=new FileStream(file,FileAccess.ReadWrite);file=null;
                    FileIdentity=Identity(info);Bytes=((long)info.SizeHigh<<32)|info.SizeLow;Attributes=info.Attributes;
                    LastWriteUtc=DateTime.FromFileTimeUtc(((long)info.WriteHigh<<32)|info.WriteLow).ToString("o");SHA256=Hash();AssertUnchanged();
                }finally{if(file!=null)file.Dispose();}
            }catch{Dispose();throw;}
        }
        public void AssertUnchanged() {
            if(stream==null)throw new ObjectDisposedException("ExecutionTargetLeaseV1");
            for(int i=0;i<directories.Count;i++){
                Info directory=Information(directories[i]);
                if((directory.Attributes&0x400)!=0||Identity(directory)!=directoryIdentities[i]||!String.Equals(Final(directories[i]),directoryPaths[i],StringComparison.OrdinalIgnoreCase))throw new IOException("Execution directory changed");
            }
            Info info=Information(stream.SafeFileHandle);
            if(info.Links!=1||info.Attributes!=Attributes||Identity(info)!=FileIdentity||((long)info.SizeHigh<<32|info.SizeLow)!=Bytes||
                DateTime.FromFileTimeUtc(((long)info.WriteHigh<<32)|info.WriteLow).ToString("o")!=LastWriteUtc||!String.Equals(Final(stream.SafeFileHandle),Path,StringComparison.OrdinalIgnoreCase)||Hash()!=SHA256)throw new IOException("Held execution target changed");
        }
        public void Dispose(){if(stream!=null){stream.Dispose();stream=null;}for(int i=directories.Count-1;i>=0;i--)directories[i].Dispose();directories.Clear();}
    }
}
