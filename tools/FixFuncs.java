import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;

public class FixFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock text = mem.getBlock(".text");
        Address start = text.getStart();
        Address end = toAddr(0x496ad5); // end of code proper (before CRT?) - use whole .text
        end = text.getEnd();
        FunctionManager fm = currentProgram.getFunctionManager();
        int created = 0;
        // 1) padding heuristic: byte after >=1 int3 (0xCC) run, where next bytes look like code start
        long a = start.getOffset();
        long e = end.getOffset();
        byte[] buf = new byte[(int)(e - a + 1)];
        mem.getBytes(start, buf);
        for (int i = 1; i < buf.length; i++) {
            int b = buf[i] & 0xff, p = buf[i-1] & 0xff;
            if (p == 0xCC && b != 0xCC) {
                Address addr = toAddr(a + i);
                if (fm.getFunctionAt(addr) == null) {
                    Function containing = fm.getFunctionContaining(addr);
                    // create function; if inside another function, Ghidra will re-bound
                    DisassembleCommand dc = new DisassembleCommand(addr, null, true);
                    dc.applyTo(currentProgram, monitor);
                    CreateFunctionCmd cmd = new CreateFunctionCmd(addr);
                    if (cmd.applyTo(currentProgram, monitor)) created++;
                }
            }
        }
        // 2) direct call targets
        Listing listing = currentProgram.getListing();
        InstructionIterator it = listing.getInstructions(text.getStart(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            if (m.equals("CALL")) {
                Address[] flows = ins.getFlows();
                for (Address t : flows) {
                    if (text.contains(t) && fm.getFunctionAt(t) == null) {
                        CreateFunctionCmd cmd = new CreateFunctionCmd(t);
                        if (cmd.applyTo(currentProgram, monitor)) created++;
                    }
                }
            }
        }
        println("FixFuncs created " + created + " functions; total=" + fm.getFunctionCount());
    }
}
