// Vault-only hard links. Never link a game or package file into the vault.
// Objects are write-once by the installer; every use verifies their bytes.
using System;
using System.IO;
using System.ComponentModel;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace Installer033 {
    public static class VaultLinksV1 {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern bool CreateHardLink(string name,string existing,IntPtr security);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
        [DllImport("kernel32.dll", SetLastError=true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle file,int kind,IntPtr data,uint size);
        [StructLayout(LayoutKind.Sequential)] private struct RenameInfo {
            public uint Flags; public IntPtr Root; public uint Length; public ushort Name;
        }
        private static bool Replace(string stage,string target) {
            byte[] name=Encoding.Unicode.GetBytes(Long(target));
            int offset=(int)Marshal.OffsetOf(typeof(RenameInfo),"Name");
            byte[] data=new byte[offset+name.Length+2];name.CopyTo(data,offset);
            IntPtr buffer=Marshal.AllocHGlobal(data.Length);
            try {
                Marshal.Copy(data,0,buffer,data.Length);
                // REPLACE_IF_EXISTS | POSIX_SEMANTICS permits replacement while
                // the old file remains open with writes denied. Never delete first.
                Marshal.WriteInt32(buffer,3);
                Marshal.WriteInt32(buffer,(int)Marshal.OffsetOf(typeof(RenameInfo),"Length"),name.Length);
                using(var file=CreateFile(Long(stage),0x10000,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero)) {
                    if(file.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
                    if(SetFileInformationByHandle(file,22,buffer,(uint)data.Length))return true;
                    int e=Marshal.GetLastWin32Error();
                    if(e==1||e==5||e==50||e==87)return false;
                    throw new Win32Exception(e,"033 vault atomic replacement failed: "+e);
                }
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        private static string Long(string p) { return @"\\?\"+Path.GetFullPath(p); }
        public static void DeleteVerified(string path,string hash) {
            using(var handle=CreateFile(Long(path),0x80000000|0x10000,5,IntPtr.Zero,3,0x00200000,IntPtr.Zero)) {
                if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
                using(var file=new FileStream(handle,FileAccess.Read)) {
                    Check(file,hash);
                    // Delete the exact checked file handle, never a freshly resolved
                    // path that could have been replaced between hashing and deletion.
                    IntPtr data=Marshal.AllocHGlobal(1);
                    try {
                        Marshal.WriteByte(data,1);
                        if(!SetFileInformationByHandle(handle,4,data,1))throw new Win32Exception(Marshal.GetLastWin32Error());
                    } finally { Marshal.FreeHGlobal(data); }
                }
            }
        }
        private static void Check(FileStream stream,string hash) {
            using(var h=SHA256.Create()) {
                if(!String.Equals(BitConverter.ToString(h.ComputeHash(stream)).Replace("-",""),hash,StringComparison.OrdinalIgnoreCase))
                    throw new IOException("033 vault content hash mismatch");
            }
        }
        public static bool Alias(string source,string target,string hash,bool replace) {
            // Holding these handles denies byte writes while checking and replacing.
            // Delete sharing is needed for the atomic rename of an existing alias.
            using(var input=new FileStream(Long(source),FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete)) {
                Check(input,hash);
                FileStream previous=null;
                string stage=replace?Path.Combine(Path.GetDirectoryName(target),".033-link-"+Guid.NewGuid().ToString("N")+".tmp"):target;
                bool created=false;
                try {
                    if(replace) { previous=new FileStream(Long(target),FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete);Check(previous,hash); }
                    if(!CreateHardLink(Long(stage),Long(source),IntPtr.Zero)) {
                        int e=Marshal.GetLastWin32Error();
                        // Unsupported FS, permissions, cross-volume, link-count limit:
                        // leave the existing backup intact, or let the caller copy.
                        if(e==1||e==5||e==17||e==50||e==1142)return false;
                        throw new Win32Exception(e,"033 vault link creation failed");
                    }
                    created=true;
                    if(replace&&!Replace(stage,target))return false;
                    created=false;
                    return true;
                } finally {
                    if(previous!=null)previous.Dispose();
                    if(created&&replace)File.Delete(Long(stage));
                }
            }
        }
    }
}
