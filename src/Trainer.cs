using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

static class Program {
    internal const string DefaultGame = @"E:\SteamLibrary\steamapps\common\Crashlands\Crashlands.exe";
    internal const string GameHash = "a0fa1008f3f9903701765dc168c25c68bba95bda248899d60e325c6a1c9a4b4d";
    [STAThread] static void Main(string[] args) {
        if (args.Length > 0 && args[0] == "--headless") {
            int seconds = args.Length > 1 ? int.Parse(args[1]) : 300;
            int flags = args.Length > 2 ? int.Parse(args[2]) : 127;
            using (var session = new TrainerSession()) {
                var end = DateTime.UtcNow.AddSeconds(seconds);
                while (DateTime.UtcNow < end) {
                    try { session.Tick(DefaultGame, flags); Console.WriteLine(session.Snapshot()); }
                    catch (Exception e) { Console.WriteLine(e.Message); }
                    System.Threading.Thread.Sleep(500);
                }
            }
            return;
        }
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new TrainerForm());
    }
}

sealed class TrainerSession : IDisposable {
    MemoryMappedFile map; MemoryMappedViewAccessor view; Process process;
    int pid; string status = "等待游戏启动";
    public string Status { get { return status; } }
    public bool Connected { get { return view != null && view.ReadInt32(12) == 1; } }
    public long Hits(int index) { return view == null ? 0 : view.ReadInt64(24 + index * 8); }
    public string Snapshot() {
        return string.Format("{0}; pid={1}; flags={2}; hooks={3}; hits={4},{5},{6},{7},{8},{9},{10}; item={11}; amount={12}; quality={13}; affixes={14}; speed={15}; instant={16}; harvest={17}",
            status,pid,view==null?0:view.ReadInt32(8),view==null?0:view.ReadInt32(20),Hits(0),Hits(1),Hits(2),Hits(3),Hits(4),Hits(5),Hits(6),
            view==null?0:view.ReadInt64(80),view==null?0:view.ReadDouble(88),view==null?0:view.ReadInt32(96),view==null?0:view.ReadInt32(100),view==null?0:view.ReadDouble(104),Hits(5),Hits(6));
    }
    public void Tick(string path, int flags) {
        if (process != null && process.HasExited) { Disconnect(); status = "游戏已退出"; }
        if (process == null) {
            foreach (var p in Process.GetProcessesByName("Crashlands")) {
                string actual;
                try { actual = p.MainModule.FileName; } catch { p.Dispose(); continue; }
                if (!string.Equals(Path.GetFullPath(actual),Path.GetFullPath(path),StringComparison.OrdinalIgnoreCase)) { p.Dispose(); continue; }
                Verify(actual); process = p; pid = p.Id;
                map = MemoryMappedFile.CreateOrOpen("Local\\CrashlandsTrainer_v2_" + pid,4096,MemoryMappedFileAccess.ReadWrite);
                view = map.CreateViewAccessor();
                view.Write(0,unchecked((int)0x43544e52)); view.Write(4,3); view.Write(16,Environment.TickCount); view.Write(8,flags);
                bool loaded=false;
                foreach (ProcessModule module in p.Modules) if(module.ModuleName.Equals("CrashlandsTrainer.dll",StringComparison.OrdinalIgnoreCase)) {
                    string expectedDll=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"CrashlandsTrainer.dll");
                    if(!string.Equals(module.FileName,expectedDll,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("游戏仍加载着其他版本的修改器，请退出游戏后重新启动。");
                    loaded=true;
                }
                if (!loaded) { Backup(); Inject(p,Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"CrashlandsTrainer.dll")); }
                status = "正在连接游戏"; break;
            }
        }
        if (view != null) {
            view.Write(16,Environment.TickCount);view.Write(8,flags);
            int state=view.ReadInt32(12);
            if(state==1) status=flags==0?"已连接，全部功能已关闭":"已连接，所选功能已启用";
            if(state<0) { view.Write(8,0); throw new InvalidOperationException("连接失败，功能未启用。请重启游戏后再试。"); }
        }
    }
    static void Verify(string path) {
        using(var sha=SHA256.Create()) using(var f=File.OpenRead(path)) {
            string hash=BitConverter.ToString(sha.ComputeHash(f)).Replace("-","").ToLowerInvariant();
            if(hash!=Program.GameHash) throw new InvalidOperationException("游戏版本不匹配。此修改器支持 Steam build 20483884，请勿用于其他版本。");
        }
    }
    static void Backup() {
        string source=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Crashlands","Game");
        if(!Directory.Exists(source)) return;
        string folder=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"存档备份"); Directory.CreateDirectory(folder);
        string output=Path.Combine(folder,DateTime.Now.ToString("yyyyMMdd_HHmmss_fff")+".zip");
        ZipFile.CreateFromDirectory(source,output,CompressionLevel.Optimal,false);
    }
    public void Off() { if(view!=null) view.Write(8,0); }
    void Disconnect() {
        Off(); if(view!=null)view.Dispose();if(map!=null)map.Dispose();if(process!=null)process.Dispose();
        view=null;map=null;process=null;pid=0;
    }
    public void Dispose() { Disconnect(); }
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int id);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr address,UIntPtr size,uint type,uint protect);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool WriteProcessMemory(IntPtr p,IntPtr address,byte[] bytes,UIntPtr size,out UIntPtr written);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateRemoteThread(IntPtr p,IntPtr attr,UIntPtr stack,IntPtr entry,IntPtr arg,uint flags,out uint id);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeThread(IntPtr h,out uint code);
    [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p,IntPtr address,UIntPtr size,uint type);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    static void Inject(Process process,string dll) {
        if(!File.Exists(dll))throw new FileNotFoundException("缺少修改器组件，请完整解压压缩包。",dll);
        IntPtr p=OpenProcess(0x43a,false,process.Id);
        if(p==IntPtr.Zero)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        IntPtr memory=IntPtr.Zero,thread=IntPtr.Zero; bool finished=false;
        try {
            byte[] bytes=Encoding.Unicode.GetBytes(Path.GetFullPath(dll)+"\0");
            memory=VirtualAllocEx(p,IntPtr.Zero,(UIntPtr)bytes.Length,0x3000,4);
            UIntPtr written;
            if(memory==IntPtr.Zero || !WriteProcessMemory(p,memory,bytes,(UIntPtr)bytes.Length,out written) || written.ToUInt64()!=(ulong)bytes.Length)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            IntPtr kernel=GetModuleHandle("kernel32.dll");IntPtr local=GetProcAddress(kernel,"LoadLibraryW");
            long remoteKernel=0;
            foreach(ProcessModule module in process.Modules) if(module.ModuleName.Equals("kernel32.dll",StringComparison.OrdinalIgnoreCase)) remoteKernel=module.BaseAddress.ToInt64();
            if(remoteKernel==0 || local==IntPtr.Zero)throw new InvalidOperationException("无法定位游戏加载器。");
            IntPtr entry=new IntPtr(remoteKernel+(local.ToInt64()-kernel.ToInt64()));uint tid;
            thread=CreateRemoteThread(p,IntPtr.Zero,UIntPtr.Zero,entry,memory,0,out tid);
            if(thread==IntPtr.Zero)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            finished=WaitForSingleObject(thread,10000)==0;
            if(!finished)throw new TimeoutException("连接超时，请等待几秒或重启游戏。");
            uint code;if(!GetExitCodeThread(thread,out code)||code==0)throw new InvalidOperationException("游戏未能加载修改器组件。");
        } finally { if(thread!=IntPtr.Zero)CloseHandle(thread);if(memory!=IntPtr.Zero&&finished)VirtualFreeEx(p,memory,UIntPtr.Zero,0x8000);CloseHandle(p); }
    }
}

sealed class TrainerForm : Form {
    readonly TrainerSession session=new TrainerSession(); readonly CheckBox[] checks=new CheckBox[7];
    readonly Label status=new Label();readonly Label[] counters=new Label[7];readonly TextBox path=new TextBox();
    readonly Timer timer=new Timer();bool errorShown;
    public TrainerForm() {
        Text="崩溃大陆 · 七项修改器 1.3.0";ClientSize=new Size(700,694);MinimumSize=new Size(716,733);MaximizeBox=false;
        Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(246,248,251);StartPosition=FormStartPosition.CenterScreen;
        Controls.Add(new Label { Text="崩溃大陆修改器",Font=new Font(Font.FontFamily,22,FontStyle.Bold),Location=new Point(26,22),AutoSize=true });
        Controls.Add(new Label { Text="选择功能后启动游戏，修改器会自动连接。",Location=new Point(29,70),AutoSize=true,ForeColor=Color.DimGray });
        string[] titles={"获取材料与制作成品数量 ×10000","最高品质装备、全词条与原生极限","符合条件的图纸首次全部掉落","人物移动速度 ×10","采集全掉落、隐藏材料首次必掉","瞬间制作","瞬间完成采集"};
        string[] descriptions={"仅放大可堆叠物品；不可堆叠物品、装备和图纸保持原数量。","制作、领取和果汁熔炉重铸均生效；全 25 种附加词条，数值遵循原生上限。","稀有图纸不再被误筛除，兼容锯齿草的三刃木爪。","只修改人物移动计算，不会随帧数累乘。","采集掉落全部有效物品；隐藏传说材料首次触发必掉。","无需等待制作倒计时，材料仍正常消耗。","采集挥动间隔缩至最短；仍需对准资源并按住采集。"};
        for(int i=0;i<7;i++) {
            int y=113+i*65;
            checks[i]=new CheckBox { Text=titles[i],Checked=true,Location=new Point(30,y),Size=new Size(570,26),Font=new Font(Font,FontStyle.Bold) };Controls.Add(checks[i]);
            Controls.Add(new Label { Text=descriptions[i],Location=new Point(52,y+29),AutoSize=true,Font=new Font(Font.FontFamily,9),ForeColor=Color.DimGray });
            counters[i]=new Label { Text="",Location=new Point(593,y+4),Size=new Size(77,22),TextAlign=ContentAlignment.MiddleRight,ForeColor=Color.FromArgb(40,110,80) };Controls.Add(counters[i]);
        }
        path.Text=Program.DefaultGame;path.SetBounds(30,576,588,28);Controls.Add(path);
        var browse=new Button { Text="浏览",Location=new Point(625,574),Size=new Size(50,30) };browse.Click+=(s,e)=>{using(var d=new OpenFileDialog { Filter="Crashlands.exe|Crashlands.exe",FileName="Crashlands.exe" })if(d.ShowDialog()==DialogResult.OK)path.Text=d.FileName;};Controls.Add(browse);
        var start=new Button { Text="启动游戏",Location=new Point(30,617),Size=new Size(125,36),BackColor=Color.FromArgb(25,100,185),ForeColor=Color.White,FlatStyle=FlatStyle.Flat };
        start.Click+=(s,e)=>{try { if(!File.Exists(path.Text))throw new FileNotFoundException("游戏路径不存在。");Process.Start(new ProcessStartInfo(path.Text){WorkingDirectory=Path.GetDirectoryName(path.Text)}); }catch(Exception x){MessageBox.Show(x.Message,Text);} };Controls.Add(start);
        var off=new Button { Text="全部关闭",Location=new Point(165,617),Size=new Size(120,36) };off.Click+=(s,e)=>{foreach(var c in checks)c.Checked=false;session.Off();};Controls.Add(off);
        status.SetBounds(303,621,372,28);status.Text="等待游戏启动";status.TextAlign=ContentAlignment.MiddleRight;Controls.Add(status);
        Controls.Add(new Label { Text="关闭后效果停用；已获得的数量、品质与词条仍会保留。",Location=new Point(30,666),AutoSize=true,Font=new Font(Font.FontFamily,9),ForeColor=Color.DimGray });
        timer.Interval=400;timer.Tick+=(s,e)=>Poll();timer.Start();FormClosing+=(s,e)=>{timer.Stop();session.Dispose();};
    }
    void Poll() {
        int flags=0;for(int i=0;i<7;i++)if(checks[i].Checked)flags|=1<<i;
        try { session.Tick(path.Text,flags);status.Text=session.Status;errorShown=false;
            for(int i=0;i<7;i++)counters[i].Text=session.Hits(i)>0?"已触发":"";
        }catch(Exception e){session.Off();status.Text="未启用："+e.Message;if(!errorShown){errorShown=true;MessageBox.Show(e.Message,Text,MessageBoxButtons.OK,MessageBoxIcon.Information);}}
    }
}
