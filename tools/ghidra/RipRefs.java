// The instructions that reference a global through a rip-relative 32-bit displacement, in a code range: address, length and the offset of the displacement inside the instruction,
// for patching them to point at another variable (native split screen: the client code reads its own copy of the number of local players).
// Usage (headless): sh tools/ghidra/g.sh -s RipRefs.java <global rva hex> <code rva lo hex> <code rva hi hex>
// Prints "site <rva> <length> <disp offset> <mnemonic>" per instruction.
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class RipRefs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		Address target = currentProgram.getImageBase().add(Long.parseLong(args[0].replace("0x", ""), 16));
		long lo = Long.parseLong(args[1].replace("0x", ""), 16) + base;
		long hi = Long.parseLong(args[2].replace("0x", ""), 16) + base;
		for (Reference r : getReferencesTo(target)) {
			long from = r.getFromAddress().getOffset();
			if (from < lo || from >= hi)
				continue;
			Instruction ins = getInstructionAt(r.getFromAddress());
			if (ins == null) {
				println("site " + Long.toHexString(from - base) + " no instruction");
				continue;
			}
			int len = ins.getLength();
			long rel = target.getOffset() - (from + len);
			byte[] b = ins.getBytes();
			int found = -1, count = 0;
			for (int i = 0; i + 4 <= len; i++) {
				long v = (b[i] & 0xffL) | ((b[i + 1] & 0xffL) << 8) | ((b[i + 2] & 0xffL) << 16) | ((long) b[i + 3] << 24);
				if (v == (int) rel) {
					found = i;
					count++;
				}
			}
			println("site " + Long.toHexString(from - base) + " " + len + " " + (count == 1 ? found : -1) + " " + ins.getMnemonicString() + " " + r.getReferenceType());
		}
	}
}
