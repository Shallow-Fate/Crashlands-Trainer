// @category Crashlands
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;

public class ExportTargets extends GhidraScript {
    public void run() throws Exception {
        String root = getScriptArgs()[0];
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fm = currentProgram.getFunctionManager();
        ArrayList<Function> targets = new ArrayList<>();
        for(String line : Files.readAllLines(Paths.get(root,"symbols.tsv"))) {
            String[] x=line.split("\t");
            Address a=sp.getAddress(Long.parseUnsignedLong(x[0],16));
            String name=x[1].replaceAll("[^A-Za-z0-9_]","_");
            try { createLabel(a,name,true); } catch(Exception e) {}
            if(x.length<3) continue;
            Function f=fm.getFunctionAt(a);
            if(f==null) {
                Address end=a.add(Long.parseLong(x[2],16)-1);
                try { f=fm.createFunction(name,a,new AddressSet(a,end),SourceType.USER_DEFINED); } catch(Exception e) { continue; }
            } else { f.setName(name,SourceType.USER_DEFINED); }
            if(name.startsWith("gml_Script_")) {
                DataType vp=new PointerDataType(VoidDataType.dataType);
                f.setReturnType(vp,SourceType.USER_DEFINED);
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,true,SourceType.USER_DEFINED,
                    new ParameterImpl("self",vp,currentProgram),new ParameterImpl("other",vp,currentProgram),
                    new ParameterImpl("result",vp,currentProgram),new ParameterImpl("argc",IntegerDataType.dataType,currentProgram),new ParameterImpl("args",new PointerDataType(vp),currentProgram));
            }
            if(x.length>3) targets.add(f);
        }
        DecompInterface decomp=new DecompInterface();
        decomp.openProgram(currentProgram);
        for(Function f:targets) {
            disassemble(f.getEntryPoint());
            println("Decompiling "+f.getName());
            DecompileResults r=decomp.decompileFunction(f,120,monitor);
            String text=r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage();
            Files.createDirectories(Paths.get(root,"decompiled"));
            Files.writeString(Paths.get(root,"decompiled",f.getName()+".c"),text);
        }
        decomp.dispose();
    }
}
