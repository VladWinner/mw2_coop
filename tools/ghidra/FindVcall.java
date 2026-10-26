// Indirect calls through a vtable slot: CALL qword ptr [reg + disp], in a code range (to find where the renderer calls Present, EndScene, ...).
// Usage (headless): sh tools/ghidra/g.sh -s FindVcall.java <code rva lo hex> <code rva hi hex> <disp hex> [<disp hex> ...]
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class FindVcall extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		long lo = Long.parseLong(args[0].replace("0x", ""), 16) + base;
		long hi = Long.parseLong(args[1].replace("0x", ""), 16) + base;
		java.util.HashSet<Long> disps = new java.util.HashSet<>();
		for (int i = 2; i < args.length; i++)
			disps.add(Long.parseLong(args[i].replace("0x", ""), 16));
		InstructionIterator it = currentProgram.getListing().getInstructions(currentProgram.getAddressFactory().getAddress(Long.toHexString(lo)), true);
		while (it.hasNext()) {
			Instruction ins = it.next();
			if (ins.getAddress().getOffset() >= hi)
				break;
			if (!ins.getMnemonicString().equals("CALL"))
				continue;
			String t = ins.toString();
			for (long d : disps) {
				if (t.contains("+ 0x" + Long.toHexString(d) + "]")) {
					Function f = getFunctionContaining(ins.getAddress());
					println("call [reg+0x" + Long.toHexString(d) + "] at rva 0x" + Long.toHexString(ins.getAddress().getOffset() - base) + " in "
							+ (f == null ? "?" : "rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base)));
				}
			}
		}
	}
}
