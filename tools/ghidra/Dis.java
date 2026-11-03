// Disassembly of an rva range: Usage (headless): GHIDRA_SCRIPT=Dis.java sh tools/ghidra/run.sh <rva hex> <byte count hex>
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

public class Dis extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		Address at = currentProgram.getImageBase().add(Long.parseLong(a[0].replace("0x", ""), 16));
		long n = Long.parseLong(a[1].replace("0x", ""), 16);
		Address end = at.add(n);
		Instruction in = getInstructionAt(at);
		if (in == null)
			in = getInstructionAfter(at);
		while (in != null && in.getAddress().compareTo(end) < 0) {
			StringBuilder hex = new StringBuilder();
			for (byte b : in.getBytes())
				hex.append(String.format("%02x ", b & 0xff));
			println("rva " + Long.toHexString(in.getAddress().getOffset() - base) + "  " + String.format("%-30s", hex) + in.toString());
			in = in.getNext();
		}
	}
}
