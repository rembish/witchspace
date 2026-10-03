// Ghidra headless pre-script: both code segments (0000, 2270) run with DS = ES = data segment 0b00
// (Ghidra 1b00), so tell the analyzer before it resolves data references.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.ProgramContext;
import java.math.BigInteger;

public class SetDS extends GhidraScript {
    @Override
    public void run() throws Exception {
        ProgramContext ctx = currentProgram.getProgramContext();
        String[][] ranges = {{"1000:0000", "1000:afff"}, {"3270:0000", "3270:300f"}};
        for (String[] rg : ranges)
            for (String r : new String[] {"ds", "es"})
                ctx.setValue(ctx.getRegister(r), toAddr(rg[0]), toAddr(rg[1]), BigInteger.valueOf(0x1b00));
        println("ds/es = 1b00 over both code segments");
    }
}
