// Ghidra headless pre-script: the code segment always runs with DS = ES = data segment 0b00
// (Ghidra 1b00), so tell the analyzer before it resolves data references.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.ProgramContext;
import java.math.BigInteger;

public class SetDS extends GhidraScript {
    @Override
    public void run() throws Exception {
        ProgramContext ctx = currentProgram.getProgramContext();
        Address start = toAddr("1000:0000"), end = toAddr("1000:ffff");
        for (String r : new String[] {"ds", "es"}) {
            Register reg = ctx.getRegister(r);
            ctx.setValue(reg, start, end, BigInteger.valueOf(0x1b00));
        }
        println("ds/es = 1b00 over code segment");
    }
}
