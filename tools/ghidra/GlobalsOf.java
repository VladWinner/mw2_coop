// The writable globals (.data/.bss) a function and everything it calls touch, grouped into address ranges: the first look at
// what a piece of the engine keeps in static memory (for the native split screen: which client-side state would have to be swapped).
// Usage (headless): GHIDRA_SCRIPT=GlobalsOf.java sh tools/ghidra/run.sh <rva hex> [max functions] [gap bytes]
// Prints "range rva 0x<lo>-0x<hi> (<n> bytes) read by <k> functions" for each cluster of addresses closer than <gap> bytes.
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

public class GlobalsOf extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		int maxFunctions = args.length > 1 ? Integer.parseInt(args[1]) : 2000;
		long gap = args.length > 2 ? Long.parseLong(args[2]) : 256;
		Address start = currentProgram.getImageBase().add(Long.parseLong(args[0].replace("0x", ""), 16));
		Function root = getFunctionAt(start);
		if (root == null)
			root = getFunctionContaining(start);
		if (root == null) {
			println("no function at " + start);
			return;
		}
		Set<Long> seen = new HashSet<>();
		ArrayDeque<Function> queue = new ArrayDeque<>();
		queue.add(root);
		seen.add(root.getEntryPoint().getOffset());
		TreeMap<Long, Set<Long>> users = new TreeMap<>(); // global address -> the functions that touch it
		while (!queue.isEmpty() && seen.size() <= maxFunctions) {
			Function f = queue.poll();
			Instruction ins = getInstructionAt(f.getEntryPoint());
			while (ins != null && f.getBody().contains(ins.getAddress())) {
				for (Reference r : ins.getReferencesFrom()) {
					Address to = r.getToAddress();
					if (r.getReferenceType().isCall()) {
						Function callee = getFunctionAt(to);
						if (callee != null && seen.add(callee.getEntryPoint().getOffset()))
							queue.add(callee);
						continue;
					}
					if (!r.getReferenceType().isData())
						continue;
					MemoryBlock b = currentProgram.getMemory().getBlock(to);
					if (b == null || !b.isWrite() || b.isExecute())
						continue;
					users.computeIfAbsent(to.getOffset(), k -> new HashSet<>()).add(f.getEntryPoint().getOffset());
				}
				ins = ins.getNext();
			}
		}
		println("functions walked: " + seen.size() + ", distinct global addresses: " + users.size());
		long lo = -1, hi = -1;
		Set<Long> fns = new HashSet<>();
		int n = 0;
		for (Map.Entry<Long, Set<Long>> e : users.entrySet()) {
			long a = e.getKey();
			if (lo >= 0 && a - hi > gap) {
				println("range rva 0x" + Long.toHexString(lo - base) + "-0x" + Long.toHexString(hi - base) + " (" + (hi - lo + 1) + " bytes, "
						+ n + " addresses) used by " + fns.size() + " functions");
				fns.clear();
				n = 0;
				lo = -1;
			}
			if (lo < 0)
				lo = a;
			hi = a;
			n++;
			fns.addAll(e.getValue());
		}
		if (lo >= 0)
			println("range rva 0x" + Long.toHexString(lo - base) + "-0x" + Long.toHexString(hi - base) + " (" + (hi - lo + 1) + " bytes, " + n
					+ " addresses) used by " + fns.size() + " functions");
	}
}
