import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/home/claude/proj/decomp.c";
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isExternal()) continue;
            DecompileResults r = ifc.decompileFunction(f, 60, monitor);
            pw.println("// ==== FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses());
            if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
                pw.println(r.getDecompiledFunction().getC());
            } else {
                pw.println("// decompile failed");
            }
            n++;
        }
        pw.close();
        println("Exported " + n + " functions to " + out);
    }
}
