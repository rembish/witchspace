// Ghidra headless post-script: plain listing (address, bytes, instruction, labels, xrefs)
// for the whole program, which reads better than decompiled C for hand-written assembly.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "listing.txt";
        Listing l = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        try (PrintWriter pw = new PrintWriter(new FileWriter(out))) {
            for (Instruction in : l.getInstructions(true)) {
                Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(in.getAddress());
                if (s != null && !s.isDynamic()) pw.println(s.getName() + ":");
                else if (s != null && rm.hasReferencesTo(in.getAddress())) {
                    StringBuilder sb = new StringBuilder();
                    for (Reference r : rm.getReferencesTo(in.getAddress())) {
                        if (sb.length() > 60) { sb.append(" ..."); break; }
                        sb.append(' ').append(r.getFromAddress().getOffset() & 0xffff);
                    }
                }
                pw.printf("%s  %-24s %s%n", in.getAddress(), in.getMnemonicString() , in.toString().substring(in.getMnemonicString().length()).trim());
            }
        }
        println("wrote " + out);
    }
}
