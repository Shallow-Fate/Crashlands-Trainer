using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

static class LauncherTests {
    static int assertions;
    static void Check(bool condition,string message) {
        assertions++;
        if(!condition)throw new Exception(message);
    }
    static string RunChild(string file,string action,string value,string workingDirectory) {
        var info=new ProcessStartInfo(Assembly.GetExecutingAssembly().Location) {
            Arguments=action+" \""+file+"\" \""+value+"\"",
            WorkingDirectory=workingDirectory,UseShellExecute=false,RedirectStandardOutput=true,StandardOutputEncoding=Encoding.UTF8,CreateNoWindow=true
        };
        using(var child=Process.Start(info)) {
            string output=child.StandardOutput.ReadToEnd();
            child.WaitForExit();
            Check(child.ExitCode==0,"Settings child process failed.");
            return output;
        }
    }
    static TextBox PathBox(TrainerForm form) {
        foreach(Control control in form.Controls)if(control is TextBox)return (TextBox)control;
        throw new Exception("Game path textbox not found.");
    }
    static void CloseForm(TrainerForm form) {
        typeof(Form).GetMethod("OnFormClosing",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(form,new object[] { new FormClosingEventArgs(CloseReason.UserClosing,false) });
        form.Dispose();
    }
    [STAThread] static int Main(string[] args) {
        if(args.Length>0) {
            Console.OutputEncoding=new UTF8Encoding(false);
            var childSettings=new GamePathSettings(args[1]);
            if(args[0]=="--save")childSettings.Save(args[2]);
            else Console.Write(childSettings.Load());
            return 0;
        }
        string root=Path.Combine(Path.GetTempPath(),"CrashlandsTrainer-settings-tests-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            string file=Path.Combine(root,"user-a","game-path.txt");
            var settings=new GamePathSettings(file);
            string first=@"D:\玩家甲\Steam Library\Crashlands\Crashlands.exe";
            string second=@"F:\Games & Mods\Crashlands\Crashlands.exe";
            Check(settings.Load()==Program.DefaultGame,"First run should use the existing default.");
            Check(!File.Exists(file),"Loading must not create a user setting to distribute.");
            RunChild(file,"--save",first,root);
            string otherDirectory=Path.Combine(root,"other-working-directory");
            Directory.CreateDirectory(otherDirectory);
            Check(RunChild(file,"--load","",otherDirectory)==first,"A fresh process must restore the chosen Unicode path from another working directory.");

            string otherFile=Path.Combine(root,"user-b","game-path.txt");
            var otherSettings=new GamePathSettings(otherFile);
            otherSettings.Save(second);
            Check(settings.Load()==first && otherSettings.Load()==second,"Users must retain independent paths.");

            using(var form=new TrainerForm(settings)) {
                Check(PathBox(form).Text==first,"The launcher must display the saved path.");
                PathBox(form).Text=second;
                typeof(Control).GetMethod("OnValidated",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(PathBox(form),new object[] { EventArgs.Empty });
                Check(new GamePathSettings(file).Load()==second,"Leaving a manually edited path must save it.");
                CloseForm(form);
            }
            using(var form=new TrainerForm(new GamePathSettings(file))) {
                Check(PathBox(form).Text==second,"Reopening the launcher must restore the last path.");
                PathBox(form).Text=first;
                CloseForm(form);
            }
            Check(new GamePathSettings(file).Load()==first,"Closing with an edit still in the textbox must save it.");
            Check(!File.Exists(first),"The test path must remain fictional.");
            Check(settings.Load()==first,"A temporarily missing game must not discard the saved path.");
            settings.Save("  ");
            Check(settings.Load()==first,"An empty edit must not erase the last saved path.");

            bool failed=false;
            using(var locked=File.Open(file,FileMode.Open,FileAccess.Read,FileShare.None)) {
                try { settings.Save(second); } catch(IOException) { failed=true; }
            }
            Check(failed,"The locked configuration should report a save failure.");
            Check(settings.Load()==first,"A failed save must preserve the previous setting.");
            Check(Directory.GetFiles(Path.GetDirectoryName(file),"*.tmp").Length==0,"Failed saves must clean up temporary files.");
            File.WriteAllText(file,"");
            Check(settings.Load()==Program.DefaultGame,"An empty settings file must not prevent startup.");
            File.Delete(file);
            Directory.CreateDirectory(file);
            Check(settings.Load()==Program.DefaultGame,"An unreadable settings location must not prevent startup.");
            string defaultFile=(string)typeof(GamePathSettings).GetField("file",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(new GamePathSettings());
            Check(defaultFile==Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"CrashlandsTrainer","game-path.txt"),"Production settings must live under the current Windows account.");
            Console.WriteLine("PASS: "+assertions+" launcher persistence assertions.");
            return 0;
        } catch(Exception e) {
            Console.Error.WriteLine(e);
            return 1;
        } finally {
            // Only the uniquely named fixture directory created above is removed.
            Directory.Delete(root,true);
        }
    }
}
