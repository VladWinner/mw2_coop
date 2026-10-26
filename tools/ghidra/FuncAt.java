// The function that contains an address: its start, its size and its callers (for a crash address).
// Usage (headless): GHIDRA_SCRIPT=FuncAt.java sh tools/ghidra/run.sh <rva hex> [more rvas]
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class FuncAt extends GhidraScript {
	@Override
	protected void run() throws Exception {
		long base = currentProgram.getImageBase().getOffset();
		for (String a : getScriptArgs()) {
			Address at = currentProgram.getImageBase().add(Long.parseLong(a.replace("0x", ""), 16));
			Function f = getFunctionContaining(at);
			if (f == null) {
				println("rva 0x" + a + ": no function");
				continue;
			}
			println("rva 0x" + a + " is in " + f.getName() + " rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base) + ", "
					+ f.getBody().getNumAddresses() + " bytes");
			int n = 0;
			for (Reference r : getReferencesTo(f.getEntryPoint())) {
				Function c = getFunctionContaining(r.getFromAddress());
				println("    " + r.getReferenceType() + " from " + (c == null ? "data " + r.getFromAddress() : "rva 0x" + Long.toHexString(c.getEntryPoint().getOffset() - base)));
				if (++n >= 8)
					break;
			}
		}
	}
}
