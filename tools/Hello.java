// @category Crashlands
import ghidra.app.script.GhidraScript;

public class Hello extends GhidraScript {
    public void run() throws Exception {
        println("HELLO " + getScriptArgs().length);
    }
}
