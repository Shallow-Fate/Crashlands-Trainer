// @category Crashlands
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;
public class DecompileMore extends GhidraScript {
    void prototype(long address,DataType ret,DataType... types) throws Exception {
        Function f=getFunctionAt(toAddr(address));
        if(f==null) { disassemble(toAddr(address));f=createFunction(toAddr(address),null); }
        if(f==null)return;
        f.setReturnType(ret,SourceType.USER_DEFINED);
        Parameter[] ps=new Parameter[types.length];
        for(int i=0;i<types.length;i++)ps[i]=new ParameterImpl("p"+i,types[i],currentProgram);
        f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,true,SourceType.USER_DEFINED,ps);
    }
    public void run() throws Exception {
        String root=getScriptArgs()[0];
        DataType vp=new PointerDataType(VoidDataType.dataType), vpp=new PointerDataType(vp);
        prototype(0x141673bd0L,vp,vp,vp,vp,IntegerDataType.dataType,IntegerDataType.dataType,vpp);
        prototype(0x140091ae0L,vp,vp,IntegerDataType.dataType);
        prototype(0x141677430L,IntegerDataType.dataType,vp);
        prototype(0x141675ef0L,BooleanDataType.dataType,vp);
        prototype(0x1416775b0L,DoubleDataType.dataType,vp);
        prototype(0x1416b9a90L,DoubleDataType.dataType,DoubleDataType.dataType);
        prototype(0x141689120L,IntegerDataType.dataType,vp,vp,DoubleDataType.dataType,BooleanDataType.dataType);
        for(long a:new long[]{0x140091080L,0x1400917e0L,0x1400d0fa0L,0x1400aa750L,0x1400aa970L})prototype(a,vp,vp,vp);
        DecompInterface dc=new DecompInterface();dc.openProgram(currentProgram);
        for(String n:Files.readAllLines(Paths.get(root,"more-targets.txt"))) {
            Function f=getGlobalFunctions(n.trim()).stream().findFirst().orElse(null);if(f==null)continue;
            disassemble(f.getEntryPoint());println("Decompiling "+n);
            DecompileResults r=dc.decompileFunction(f,90,monitor);
            Files.writeString(Paths.get(root,"decompiled",n+".c"),r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());
        }
        dc.dispose();
    }
}
