// Callers of a function, level by level (breadth first), to see how the engine reaches it from its frame loop.
// Usage (headless): GHIDRA_SCRIPT=CallChain.java sh tools/ghidra/run.sh <rva hex> [depth]
// Prints "L<depth> rva <caller> (<name>) calls <callee rva>" for every caller found; a function reached again is not walked again.
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;

public class CallChain extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		int depth = args.length > 1 ? Integer.parseInt(args[1]) : 6;
		Address start = currentProgram.getImageBase().add(Long.parseLong(args[0].replace("0x", ""), 16));
		Function first = getFunctionAt(start);
		if (first == null)
			first = getFunctionContaining(start);
		if (first == null) {
			println("no function at " + start);
			return;
		}
		Set<Long> seen = new HashSet<>();
		ArrayDeque<Object[]> queue = new ArrayDeque<>();
		queue.add(new Object[] {first, 0});
		seen.add(first.getEntryPoint().getOffset());
		while (!queue.isEmpty()) {
			Object[] item = queue.poll();
			Function f = (Function) item[0];
			int level = (Integer) item[1];
			if (level >= depth)
				continue;
			for (Reference r : getReferencesTo(f.getEntryPoint())) {
				if (!r.getReferenceType().isCall() && !r.getReferenceType().isData())
					continue;
				Function caller = getFunctionContaining(r.getFromAddress());
				if (caller == null) {
					println("L" + (level + 1) + " " + r.getReferenceType() + " from " + r.getFromAddress() + " (data) -> rva 0x"
							+ Long.toHexString(f.getEntryPoint().getOffset() - base));
					continue;
				}
				long entry = caller.getEntryPoint().getOffset();
				println("L" + (level + 1) + " rva 0x" + Long.toHexString(entry - base) + " (" + caller.getName() + ") calls rva 0x"
						+ Long.toHexString(f.getEntryPoint().getOffset() - base));
				if (seen.add(entry))
					queue.add(new Object[] {caller, level + 1});
			}
		}
	}
}
