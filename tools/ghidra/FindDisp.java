// List functions with instructions that store to (or read from) memory at a given displacement,
// e.g. "+ 0x208]". Arguments: <disp hex> [store|load|any] [max functions].
// Usage (headless): -postScript FindDisp.java 0x208 store
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import java.util.LinkedHashMap;
import java.util.Map;

public class FindDisp extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		String disp = args[0].toLowerCase();
		String mode = args.length > 1 ? args[1] : "store";
		int max = args.length > 2 ? Integer.parseInt(args[2]) : 60;
		String needle = "+ " + disp + "]";
		long base = currentProgram.getImageBase().getOffset();
		Map<String, Integer> hits = new LinkedHashMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext() && hits.size() < max) {
			Instruction ins = it.next();
			if (ins.getNumOperands() < 1)
				continue;
			String op0 = ins.getDefaultOperandRepresentation(0).toLowerCase();
			String op1 = ins.getNumOperands() > 1 ? ins.getDefaultOperandRepresentation(1).toLowerCase() : "";
			boolean store = op0.contains(needle) && ins.getMnemonicString().startsWith("MOV");
			boolean load = op1.contains(needle);
			if (mode.equals("store") ? !store : mode.equals("load") ? !load : !(store || load))
				continue;
			Function f = getFunctionContaining(ins.getAddress());
			String key = f == null ? "?" : f.getName() + " (rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base) + ")";
			hits.merge(key, 1, Integer::sum);
		}
		println("=== " + mode + " " + needle + ": " + hits.size() + " functions");
		for (Map.Entry<String, Integer> e : hits.entrySet())
			println("  " + e.getKey() + " x" + e.getValue());
	}
}
