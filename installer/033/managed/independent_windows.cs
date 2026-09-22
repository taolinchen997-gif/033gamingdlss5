// Filesystem identity and the runtime's persistent settings byte lock only.
// No process creation, GPU/SDK, service, registry or driver operations.
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace Installer033 {
    public static class FileIdentityV1 {
        [StructLayout(LayoutKind.Sequential)] private struct Info {
            public uint Attributes,CreationLow,CreationHigh,AccessLow,AccessHigh,WriteLow,WriteHigh,Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
        }
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern uint GetFinalPathNameByHandle(SafeFileHandle handle,StringBuilder path,uint length,uint flags);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info info);
        public static uint LinkCount(string name) {
            using(var handle=CreateFile(name,0x80,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero)) {
                Info info;if(handle.IsInvalid||!GetFileInformationByHandle(handle,out info))throw new Win32Exception(Marshal.GetLastWin32Error());
                return info.Links;
            }
        }
        public static string Identity(string name) {
            using(var handle=CreateFile(name,0x80,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero)) {
                Info info;if(handle.IsInvalid||!GetFileInformationByHandle(handle,out info))throw new Win32Exception(Marshal.GetLastWin32Error());
                return info.Volume.ToString("x8")+":"+info.IndexHigh.ToString("x8")+info.IndexLow.ToString("x8");
            }
        }
        public static string DirectoryPath(string name) {
            using(var handle=CreateFile(name,0x80,3,IntPtr.Zero,3,0x02000000|0x00200000,IntPtr.Zero)) {
                if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
                var path=new StringBuilder(32768);uint count=GetFinalPathNameByHandle(handle,path,32768,0);
                if(count==0||count>=32768)throw new Win32Exception(Marshal.GetLastWin32Error());
                string result=path.ToString();
                if(result.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase))throw new IOException("Remote target roots are unsupported");
                if(result.StartsWith(@"\\?\",StringComparison.Ordinal))result=result.Substring(4);
                return result.Length>3?result.TrimEnd('\\'):result;
            }
        }
    }
    public sealed class SettingsLockV1 : IDisposable {
        [StructLayout(LayoutKind.Sequential)] private struct Range { public IntPtr Internal,InternalHigh;public uint Offset,OffsetHigh;public IntPtr Event; }
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool LockFileEx(SafeFileHandle handle,uint flags,uint reserved,uint low,uint high,ref Range range);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool UnlockFileEx(SafeFileHandle handle,uint reserved,uint low,uint high,ref Range range);
        private SafeFileHandle handle;private Range range;private bool owned;
        public SettingsLockV1(string path) {
            // Match config_store.h: persistent *.033lock, sharing READ|WRITE,
            // exclusive nonblocking byte range at offset 0, length 1.
            handle=CreateFile(path,0xc0000000,3,IntPtr.Zero,4,2,IntPtr.Zero);
            if(handle.IsInvalid){int error=Marshal.GetLastWin32Error();handle.Dispose();throw new Win32Exception(error);}
            owned=LockFileEx(handle,3,0,1,0,ref range);
            if(!owned){int error=Marshal.GetLastWin32Error();handle.Dispose();throw new Win32Exception(error,"Common settings lock is busy");}
        }
        public void Dispose(){if(handle!=null){if(owned)UnlockFileEx(handle,0,1,0,ref range);owned=false;handle.Dispose();handle=null;}}
    }
}
