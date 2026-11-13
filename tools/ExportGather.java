// @category Crashlands
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
import java.util.*;

public class ExportGather extends GhidraScript {
    public void run() throws Exception {
        String root = getScriptArgs()[0];
        HashSet<String> wanted = new HashSet<String>();
        for (int i = 1; i < getScriptArgs().length; i++) wanted.add(getScriptArgs()[i]);
        HashMap<String, Long> addrs = new HashMap<String, Long>();
        HashMap<String, Long> sizes = new HashMap<String, Long>();
        for (String line : Files.readAllLines(Paths.get(root, "symbols.tsv"))) {
            String[] x = line.split("\t");
            if (x.length > 1) {
                addrs.put(x[1], Long.parseUnsignedLong(x[0], 16));
                sizes.put(x[1], x.length > 2 ? Long.parseLong(x[2], 16) : 256L);
            }
        }
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (String name : wanted) {
            Long va = addrs.get(name);
            if (va == null) { println("MISSING SYMBOL " + name); continue; }
            Address a = sp.getAddress(va.longValue());
            Function f = fm.getFunctionAt(a);
            if (f == null) {
                Address end = a.add(sizes.get(name).longValue() - 1);
                try { f = fm.createFunction(name, a, new AddressSet(a, end), SourceType.USER_DEFINED); }
                catch (Exception e) { println("CREATE FAILED " + name + " " + e); continue; }
            }
            if (name.startsWith("gml_Script_")) {
                DataType vp = new PointerDataType(VoidDataType.dataType);
                f.setReturnType(vp, SourceType.USER_DEFINED);
                f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED,
                    new ParameterImpl("self", vp, currentProgram), new ParameterImpl("other", vp, currentProgram),
                    new ParameterImpl("result", vp, currentProgram), new ParameterImpl("argc", IntegerDataType.dataType, currentProgram),
                    new ParameterImpl("args", new PointerDataType(vp), currentProgram));
            }
            disassemble(f.getEntryPoint());
            println("Decompiling " + name);
            DecompileResults r = decomp.decompileFunction(f, 180, monitor);
            String text = r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage();
            Files.createDirectories(Paths.get(root, "decompiled"));
            Files.writeString(Paths.get(root, "decompiled", name + ".c"), text);
        }
        decomp.dispose();
    }
}
