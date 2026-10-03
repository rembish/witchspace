// Ghidra headless post-script: disassemble and create a function at every entry point found
// by re/tools/explore.py (lines "SEG:OFF callers", load-relative segments; Ghidra adds 1000).
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import java.nio.file.*;

public class CreateFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        int n = 0;
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            String[] p = line.trim().split("[:\\s]+");
            if (p.length < 2) continue;
            int seg = Integer.parseInt(p[0], 16) + 0x1000;
            Address a = toAddr(String.format("%04x:%s", seg, p[1]));
            disassemble(a);
            if (getFunctionAt(a) == null && new CreateFunctionCmd(a).applyTo(currentProgram)) n++;
        }
        println("created " + n + " functions");
    }
}
