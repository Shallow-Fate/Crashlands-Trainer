// @category Crashlands
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;

public class ExportSave extends GhidraScript {
    public void run() throws Exception {
        String root = getScriptArgs()[0];
        Set<String> wanted = new HashSet<>(Arrays.asList(getScriptArgs()).subList(1, getScriptArgs().length));
        Map<String,long[]> symbols = new HashMap<>();
        for(String line : Files.readAllLines(Paths.get(root,"symbols.tsv"))) {
            String[] x=line.split("\t");
            if(x.length>1) symbols.put(x[1], new long[]{Long.parseUnsignedLong(x[0],16), x.length>2?Long.parseLong(x[2],16):0x100});
        }
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp=new DecompInterface();
        decomp.openProgram(currentProgram);
        for(String name : wanted) {
            long[] info=symbols.get(name);
            if(info==null) { println("MISSING SYMBOL "+name); continue; }
            Address a=sp.getAddress(info[0]);
            Function f=fm.getFunctionAt(a);
            if(f==null) {
                Address end=a.add(info[1]-1);
                try { f=fm.createFunction(name,a,new AddressSet(a,end),SourceType.USER_DEFINED); }
                catch(Exception e) { println("CREATE FAILED "+name+" "+e); continue; }
            }
            if(name.startsWith("gml_Script_")) {
                DataType vp=new PointerDataType(VoidDataType.dataType);
                f.setReturnType(vp,SourceType.USER_DEFINED);
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,true,SourceType.USER_DEFINED,
                    new ParameterImpl("self",vp,currentProgram),new ParameterImpl("other",vp,currentProgram),
                    new ParameterImpl("result",vp,currentProgram),new ParameterImpl("argc",IntegerDataType.dataType,currentProgram),
                    new ParameterImpl("args",new PointerDataType(vp),currentProgram));
            }
            disassemble(f.getEntryPoint());
            println("Decompiling "+name);
            DecompileResults r=decomp.decompileFunction(f,180,monitor);
            String text=r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage();
            Files.createDirectories(Paths.get(root,"decompiled"));
            Files.writeString(Paths.get(root,"decompiled",name+".c"),text);
        }
        decomp.dispose();
    }
}
