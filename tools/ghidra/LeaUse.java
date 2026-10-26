// Where the address of a global is taken (LEA / MOV of its address), with the next few instructions: shows memset(&global, 0, size) and similar.
// Usage (headless): sh tools/ghidra/g.sh -s LeaUse.java <rva hex> [instructions after, default 8] [max hits, default 12]
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class LeaUse extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		Address target = currentProgram.getImageBase().add(Long.parseLong(args[0].replace("0x", ""), 16));
		int after = args.length > 1 ? Integer.parseInt(args[1]) : 8;
		int max = args.length > 2 ? Integer.parseInt(args[2]) : 12;
		int hits = 0;
		for (Reference r : getReferencesTo(target)) {
			Instruction ins = getInstructionAt(r.getFromAddress());
			if (ins == null || !ins.getMnemonicString().equals("LEA"))
				continue;
			Function f = getFunctionContaining(ins.getAddress());
			println("-- at rva 0x" + Long.toHexString(ins.getAddress().getOffset() - base) + " in "
					+ (f == null ? "?" : "rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base)));
			Instruction n = ins;
			for (int i = 0; i < after && n != null; i++) {
				println("   " + n.getAddress() + "  " + n);
				n = n.getNext();
			}
			if (++hits >= max)
				break;
		}
	}
}
