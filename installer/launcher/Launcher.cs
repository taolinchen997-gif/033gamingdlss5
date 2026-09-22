using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Forms;
internal static class Program {
    [STAThread] private static int Main() {
        try {
            var root=AppDomain.CurrentDomain.BaseDirectory;
            var script=Path.Combine(root,"033","dlss5_install.ps1");
            if(!File.Exists(script))throw new FileNotFoundException("请完整解压安装包。 / Extract the complete package.",script);
            var command="& '"+script.Replace("'","''")+"'; exit 0";
            var info=new ProcessStartInfo {
                FileName=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"System32","WindowsPowerShell","v1.0","powershell.exe"),
                Arguments="-NoProfile -Sta -ExecutionPolicy Bypass -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(command)),
                WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden
            };
            using(var child=Process.Start(info)){child.WaitForExit();return child.ExitCode;}
        }catch(Exception error){MessageBox.Show(error.Message,"033",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
    }
}
