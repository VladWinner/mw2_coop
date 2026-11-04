// The functions that reference addresses in a range of the image, with how many references each has (a first look at what a block of globals is for).
// Usage: sh tools/ghidra/g.sh -s RefsInRange.java <first rva hex> <last rva hex> [top n]
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.HashMap;
import java.util.Map;

public class RefsInRange extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long lo = Long.parseLong(args[0].replace("0x", ""), 16);
		long hi = Long.parseLong(args[1].replace("0x", ""), 16);
		int top = args.length > 2 ? Integer.parseInt(args[2]) : 25;
		Address base = currentProgram.getImageBase();
		Map<Long, Integer> hits = new HashMap<>();
		Map<Long, Long> firstAddr = new HashMap<>();
		for (long a = lo; a < hi; a++) {
			Address target = base.add(a);
			for (Reference r : getReferencesTo(target)) {
				Function f = getFunctionContaining(r.getFromAddress());
				if (f == null)
					continue;
				long e = f.getEntryPoint().getOffset() - base.getOffset();
				hits.merge(e, 1, Integer::sum);
				firstAddr.putIfAbsent(e, a);
			}
		}
		hits.entrySet().stream().sorted((x, y) -> y.getValue() - x.getValue()).limit(top)
			.forEach(en -> println(String.format("  rva 0x%x  %d refs  (first global 0x%x)", en.getKey(), en.getValue(), firstAddr.get(en.getKey()))));
	}
}
