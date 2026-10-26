// Finds "zero this global" calls: LEA reg,[global] ... MOV R8D,imm ... CALL, in a range of the image; prints global, size and the function. The sizes of the engine's big
// static structs show up this way (their init functions clear them).
// Usage (headless): sh tools/ghidra/g.sh -s MemsetScan.java <rva lo hex> <rva hi hex> [min size hex, default 400]
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class MemsetScan extends GhidraScript {
	@Override
	protected void run() throws Exception {
		try {
			scan();
		} catch (Throwable t) {
			println("MemsetScan failed: " + t);
		}
	}

	private void scan() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		long lo = Long.parseLong(args[0].replace("0x", ""), 16) + base;
		long hi = Long.parseLong(args[1].replace("0x", ""), 16) + base;
		long min = args.length > 2 ? Long.parseLong(args[2].replace("0x", ""), 16) : 0x400;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		long leas = 0, total = 0;
		println("scanning");
		while (it.hasNext()) {
			Instruction ins = it.next();
			total++;
			if (!ins.getMnemonicString().equals("LEA"))
				continue;
			Address target = null;
			for (ghidra.program.model.symbol.Reference r : ins.getReferencesFrom()) {
				if (r.getToAddress().getOffset() >= lo && r.getToAddress().getOffset() < hi)
					target = r.getToAddress();
			}
			if (target == null)
				continue;
			leas++;
			long size = -1;
			boolean call = false;
			Instruction n = ins.getNext();
			for (int i = 0; i < 6 && n != null; i++, n = n.getNext()) {
				if (n.getMnemonicString().equals("MOV") && n.getNumOperands() == 2 && n.getRegister(0) != null
						&& n.getRegister(0).getName().equals("R8D") && n.getOpObjects(1).length > 0 && n.getOpObjects(1)[0] instanceof Scalar)
					size = ((Scalar) n.getOpObjects(1)[0]).getUnsignedValue();
				if (n.getMnemonicString().equals("CALL")) {
					call = true;
					break;
				}
			}
			if (call && size >= min) {
				Function f = getFunctionContaining(ins.getAddress());
				println("rva 0x" + Long.toHexString(target.getOffset() - base) + " size 0x" + Long.toHexString(size) + " at rva 0x"
						+ Long.toHexString(ins.getAddress().getOffset() - base) + " in " + (f == null ? "?" : "rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base)));
			}
		}
		println("instructions " + total + ", LEA into range " + leas);
	}
}
