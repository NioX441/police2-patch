// Ghidra headless script: apply recovered Delphi names, then decompile game code to a text file.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.util.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String namesFile = args[0], outFile = args[1];
        long lo = Long.decode(args[2]), hi = Long.decode(args[3]);
        Listing listing = currentProgram.getListing();
        BufferedReader br = new BufferedReader(new FileReader(namesFile));
        String line; Map<String,Integer> used = new HashMap<>();
        while ((line = br.readLine()) != null) {
            String[] p = line.trim().split(" ", 2);
            if (p.length < 2) continue;
            Address a = toAddr(Long.decode(p[0]));
            String n = p[1].replaceAll("[^A-Za-z0-9_.]", "_");
            int c = used.merge(n, 1, Integer::sum);
            if (c > 1) n = n + "_" + c;
            Function f = listing.getFunctionAt(a);
            if (f == null) {
                if (listing.getInstructionAt(a) == null) disassemble(a);
                new CreateFunctionCmd(a).applyTo(currentProgram);
                f = listing.getFunctionAt(a);
            }
            if (f != null) f.setName(n.replace('.', '_'), SourceType.USER_DEFINED);
        }
        br.close();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(outFile));
        FunctionIterator it = listing.getFunctions(toAddr(lo), true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.getEntryPoint().getOffset() >= hi) break;
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            pw.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
            if (r != null && r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC());
            else pw.println("// decompile failed");
        }
        pw.close();
    }
}
