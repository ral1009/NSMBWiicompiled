using System.Text;
using System.Text.RegularExpressions;
using Translator.Core.IO;
using Translator.Core.Loading;
using Translator.Core.Mods;

namespace Translator.Core.Build;

/// <summary>
/// One already-translated project (main.dol or a single REL): its release-eligible
/// metadata and the function .cpp files translate-recursive wrote for it.
/// </summary>
public sealed record NsmbwModuleInput(
    string Id,
    string BaseMetadataPath,
    string FunctionsDirectory,
    uint OwnedStart = 0u,
    uint OwnedEnd = 0u)
{
    /// True when this module's own image covers the address. A REL declares its resident range
    /// in the modules file; main.dol declares none and so owns nothing exclusively.
    public bool Owns(uint address) => OwnedEnd > OwnedStart && address >= OwnedStart && address < OwnedEnd;
}

public sealed record NsmbwShardOptions(
    IReadOnlyList<NsmbwModuleInput> Modules,
    string OutputDirectory,
    string NativeSourceDirectory,
    int ShardCount = 200,
    int RegistrationShardCount = 32,
    RuntimeNativeIndex? NativeIndex = null);

public sealed record NsmbwShardResult(
    int UniqueFunctionCount,
    int DuplicateFunctionCount,
    int OptimizationVariantCount,
    int StateFreeAbiConflictCount,
    int ShardCount,
    string CMakeManifestPath);

/// <summary>
/// Emits one combined CMake shard graph for NSMBW's shape: main.dol plus four RELs that are
/// always linked into a single executable together, never selected as alternatives the way
/// WiiCompiled's base/Retro Rewind profiles are. That means none of
/// <see cref="TranslatedBuildShardEmitter"/>'s profile-sensitive/mod-overlay machinery
/// applies here - every module's functions live at disjoint, fixed addresses (a REL's
/// load_address is fixed for this build, unlike a general Wii OSLink scenario), so this is
/// a much simpler "merge and dedupe, then shard once" pass. It is declared as another part of
/// the <c>partial</c> TranslatedBuildShardEmitter class specifically to reuse its private
/// function-reading, sharding and registration-table helpers verbatim instead of forking them.
/// </summary>
public static partial class TranslatedBuildShardEmitter
{
    public static NsmbwShardResult EmitNsmbw(NsmbwShardOptions options)
    {
        if (options.Modules.Count == 0)
        {
            throw new ArgumentException("At least one module is required.", nameof(options));
        }
        if (!Directory.Exists(options.NativeSourceDirectory))
        {
            throw new DirectoryNotFoundException(options.NativeSourceDirectory);
        }
        foreach (var module in options.Modules)
        {
            if (!File.Exists(module.BaseMetadataPath))
            {
                throw new FileNotFoundException($"Missing metadata for module '{module.Id}'.", module.BaseMetadataPath);
            }
            if (!Directory.Exists(module.FunctionsDirectory))
            {
                throw new DirectoryNotFoundException(module.FunctionsDirectory);
            }
        }

        var outputRoot = Path.GetFullPath(options.OutputDirectory);
        Directory.CreateDirectory(outputRoot);

        var nativeOverrides = ReadNativeOverrides(options.NativeSourceDirectory, options.NativeIndex);

        // Every REL's recursive walk also re-discovers whatever shared low-level dol functions
        // its own startup code calls, so the same address shows up translated once per module
        // (see docs/MASTER_PLAN.md's Phase 5 log). Modules are merged in the order given, first
        // occurrence wins - functionally arbitrary since translation is deterministic per address,
        // but the fingerprint check below turns "arbitrary" into "verified identical" instead of
        // silently trusting that.
        var merged = new Dictionary<uint, FunctionRecord>();
        var moduleByAddressOwner = new Dictionary<uint, bool>();
        var duplicateCount = 0;
        var optimizationVariantCount = 0;
        foreach (var module in options.Modules)
        {
            var metadata = BaseTranslationOutputMetadataFile.Read(module.BaseMetadataPath);
            metadata.RequireReleaseEligible(module.BaseMetadataPath);
            var records = ReadBaseFunctions(metadata, module.FunctionsDirectory, sourceBundle: null);
            foreach (var record in records)
            {
                if (merged.TryGetValue(record.Address, out var existing))
                {
                    duplicateCount++;
                    // Ownership beats first-seen. Every module's project maps all four RELs so
                    // cross-module relocations can resolve, so a module's recursive walk can wander
                    // into a different REL's image and translate bytes there. Those copies are not
                    // the "same function translated twice" this merge otherwise assumes - they are
                    // one module's decode of another module's code. Confirmed for 0x80768680,
                    // d_profileNP's _prolog: d_basesNP also produced a func_80768680 holding
                    // d_basesNP's own prolog body (r3 = 0x80933864 = d_basesNP's _ctors,
                    // lr = 0x8076D878) and, being listed first, won. The runtime's REL prolog call
                    // therefore ran d_basesNP's constructors and never d_profileNP's, leaving
                    // fProfListMg_c::m_data_p (guest 0x8042A698) null so fBase_make virtual-called
                    // through it. When the incoming record's module owns the address and the
                    // incumbent's does not, the owner replaces it.
                    var incumbentOwns = moduleByAddressOwner.TryGetValue(record.Address, out var owns) && owns;
                    if (!incumbentOwns && module.Owns(record.Address))
                    {
                        merged[record.Address] = record;
                        moduleByAddressOwner[record.Address] = true;
                        continue;
                    }
                    // Leaf inlining and other interprocedural optimizations are decided per
                    // translate-recursive run from whatever call graph is reachable from that
                    // run's own seed - so the same address can legitimately come out byte-different
                    // (e.g. one run inlines a leaf callee the other calls directly) while still being
                    // semantically identical PPC. Confirmed by hand for 0x8001BB10 (main.dol inlines
                    // 0x8016DE60, d_basesNP calls it) before relaxing this from a hard mismatch error.
                    // Either copy is correct to keep; the first one seen wins.
                    if (!string.Equals(existing.SourceFingerprint, record.SourceFingerprint, StringComparison.Ordinal))
                    {
                        optimizationVariantCount++;
                    }
                    continue;
                }
                merged[record.Address] = record;
                moduleByAddressOwner[record.Address] = module.Owns(record.Address);
            }
        }

        foreach (var record in merged.Values)
        {
            record.ExcludedByNativeOverride = nativeOverrides.TranslationExclusions.Contains(record.Address);
        }
        IReadOnlyList<FunctionRecord> activeFunctions =
            merged.Values.Where(static record => !record.ExcludedByNativeOverride).ToArray();

        // "State-free" ABI narrowing is a per-run interprocedural optimization: translate-recursive
        // decides, from whatever call graph is reachable in *that* run, whether a target's callers
        // can use a smaller, specialized register signature instead of the full CpuContext. Because
        // each of the 5 modules was translated independently, they can disagree about a shared
        // target's narrowed shape - confirmed for 0x801C9BA0: main.dol/d_en_bossNP/d_enemiesNP/
        // d_profileNP all settled on `void func_801C9BA0_statefree(uint32_t,uint32_t)`, but
        // d_basesNP's own copy of that same function needed a wider `MkwStateFreeResult2`-returning
        // variant. Only one copy of the target survives the merge above, so any caller whose own
        // declaration doesn't match what actually survived would either fail to compile (two
        // conflicting extern "C" declarations landing in the same shard - how this was first
        // caught) or, worse, silently link against a mismatched signature. The call is always
        // guarded at runtime by MkwStateFreeAbiEnabled(...) with a safe InvokeDirectCpu<>() fallback,
        // so dropping just the mismatched callers (not the target) costs a little coverage but keeps
        // every remaining function's behavior correct - the alternative (rewriting caller bodies to
        // match) needs each run to share one consistent ABI choice, which is future translator work.
        var definedStateFreeSignatures = new Dictionary<string, string>(StringComparer.Ordinal);
        var sourceCache = new Dictionary<uint, string>();
        string SourceOf(FunctionRecord record)
        {
            if (!sourceCache.TryGetValue(record.Address, out var text))
            {
                text = File.ReadAllText(record.SourcePath);
                sourceCache[record.Address] = text;
            }
            return text;
        }
        static string NormalizeStateFreeSignature(string returnType, string parameters)
        {
            var paramTypes = parameters
                .Split(',', StringSplitOptions.RemoveEmptyEntries)
                .Select(static parameter => parameter.Trim()
                    .Split(' ', StringSplitOptions.RemoveEmptyEntries)
                    .FirstOrDefault() ?? string.Empty);
            return $"{returnType.Trim()}({string.Join(",", paramTypes)})";
        }
        foreach (var record in activeFunctions)
        {
            foreach (Match match in StateFreeSignatureRegex().Matches(SourceOf(record)))
            {
                if (match.Groups["semi"].Success) continue; // a caller's forward declaration, not the definition
                definedStateFreeSignatures[match.Groups["sym"].Value] =
                    NormalizeStateFreeSignature(match.Groups["ret"].Value, match.Groups["params"].Value);
            }
        }
        // Per caller, which target addresses' _statefree forward declarations disagree with
        // whatever module's copy of that target actually won the merge above. Keyed by target
        // address (not just a caller/mismatched flag) because a caller can have several
        // state-free call sites and only some of them may be affected.
        var mismatchedTargetsByCaller = new Dictionary<uint, HashSet<uint>>();
        foreach (var record in activeFunctions)
        {
            foreach (Match match in StateFreeSignatureRegex().Matches(SourceOf(record)))
            {
                if (!match.Groups["semi"].Success) continue; // this record's own definition, already recorded above
                var symbol = match.Groups["sym"].Value;
                // Absent, not just mismatched, is just as unsafe: it means every module that
                // defined this exact _statefree variant lost the merge, so nothing in the build
                // actually provides the symbol this caller forward-declared.
                var mismatched = !definedStateFreeSignatures.TryGetValue(symbol, out var authoritative)
                    || !string.Equals(
                        NormalizeStateFreeSignature(match.Groups["ret"].Value, match.Groups["params"].Value),
                        authoritative, StringComparison.Ordinal);
                if (!mismatched) continue;

                // The address is embedded in the symbol itself (func_XXXXXXXX_statefree[_vN]) -
                // no separate lookup needed to know which call site(s) to downgrade below.
                var targetAddress = uint.Parse(
                    symbol.AsSpan("func_".Length, 8),
                    System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture);
                if (!mismatchedTargetsByCaller.TryGetValue(record.Address, out var targets))
                {
                    targets = new HashSet<uint>();
                    mismatchedTargetsByCaller[record.Address] = targets;
                }
                targets.Add(targetAddress);
            }
        }
        var stateFreeMismatchedCallers = mismatchedTargetsByCaller.Count;
        if (mismatchedTargetsByCaller.Count > 0)
        {
            // Downgrade just the affected call site(s) to their already-correct, unconditional
            // InvokeDirectCpu fallback instead of dropping the whole caller - the fallback works
            // regardless of what state-free shape (if any) the winning definition of the target
            // actually has, since it never touches the state-free symbol at all. This keeps every
            // other line of the caller (its real logic) intact rather than losing the function
            // outright over a merge disagreement about one unrelated callee's ABI narrowing.
            activeFunctions = activeFunctions
                .Select(record =>
                {
                    if (!mismatchedTargetsByCaller.TryGetValue(record.Address, out var targets))
                        return record;
                    var rewritten = SourceOf(record);
                    foreach (var target in targets)
                        rewritten = DowngradeStateFreeCallSiteToSafePath(rewritten, target);
                    return new FunctionRecord
                    {
                        Address = record.Address,
                        Symbol = record.Symbol,
                        Name = record.Name,
                        SourcePath = record.SourcePath,
                        SourceFingerprint = record.SourceFingerprint,
                        RegistrationKind = record.RegistrationKind,
                        Priority = record.Priority,
                        ModuleId = record.ModuleId,
                        PreservesNonvolatileFprs = record.PreservesNonvolatileFprs,
                        NonvolatileFprWriteMask = record.NonvolatileFprWriteMask,
                        DirectCalls = record.DirectCalls,
                        CompileCostWeight = record.CompileCostWeight,
                        SourceText = rewritten,
                        ExcludedByNativeOverride = record.ExcludedByNativeOverride,
                    };
                })
                .ToArray();
        }

        // No Retro-Rewind-style profile overlay exists for NSMBW, so there is only one trait
        // table: native overrides win, everything else is the recursively translated function.
        var traits = activeFunctions.ToDictionary(
            static record => record.Address,
            record =>
            {
                var nativeWinner = nativeOverrides.Winners.Contains(record.Address);
                return new Trait(
                    Available: !nativeWinner,
                    record.PreservesNonvolatileFprs,
                    record.NonvolatileFprWriteMask,
                    MustRemainDynamicallyDispatchable: nativeWinner,
                    WinnerSymbol: nativeWinner ? null : record.Symbol,
                    WinnerKind: nativeWinner ? "native" : "base",
                    WinnerPriority: nativeWinner ? 10_000u : 0u);
            });
        foreach (var (address, rawOverride) in nativeOverrides.RawTranslatedOverrides)
        {
            traits[address] = new Trait(
                Available: true,
                rawOverride.PreservesNonvolatileFprs,
                rawOverride.NonvolatileFprWriteMask,
                MustRemainDynamicallyDispatchable: false,
                rawOverride.Symbol,
                WinnerKind: "base",
                WinnerPriority: 0u);
        }

        var shards = WriteFunctionShards(
            outputRoot, "nsmbw_all", activeFunctions, options.ShardCount, traits,
            "NSMBW combined modules", weightedSequential: true);

        var registration = WriteRegistrationShards(
            outputRoot, "nsmbw", activeFunctions, traits, options.RegistrationShardCount).ToList();
        registration.Add(WriteIndirectDispatchTable(outputRoot, "nsmbw", traits));

        var cmakeManifestPath = Path.Combine(outputRoot, "shards.cmake");
        WriteNsmbwCMakeManifest(cmakeManifestPath, outputRoot, activeFunctions.Count, shards, registration);
        PruneStaleShardSources(outputRoot, shards, registration, Array.Empty<string>());

        return new NsmbwShardResult(
            activeFunctions.Count, duplicateCount, optimizationVariantCount, stateFreeMismatchedCallers,
            shards.Count, cmakeManifestPath);
    }

    [GeneratedRegex(
        """extern\s+"C"\s+(?:MKW_PPC_\w+\s+)*(?<ret>[\w:<>]+)\s+(?<sym>func_[0-9A-Fa-f]{8}_statefree(?:_v\d+)?)\s*\((?<params>[^)]*)\)\s*(?<semi>;)?""")]
    private static partial Regex StateFreeSignatureRegex();

    /// <summary>
    /// Replaces one specific state-free call site -
    /// <c>if (MkwStateFreeAbiEnabled(0x&lt;targetAddress&gt;u) &amp;&amp; ...) { &lt;fast path&gt; } else { &lt;safe path&gt; }</c>
    /// - with just the safe path's body, for every occurrence targeting <paramref name="targetAddress"/>
    /// in <paramref name="body"/>, then strips that address's now-unreferenced <c>_statefree</c>/
    /// <c>_statefree_vN</c> forward declarations. The safe path is a plain
    /// <c>InvokeDirectCpu&lt;target&gt;(ctx)</c> call that never touches the target's _statefree symbol,
    /// so it stays correct regardless of what state-free shape (if any) the winning cross-module
    /// definition of that target actually has. Leaving the mismatched declaration in place after the
    /// call site no longer uses it is not just dead code: if another function sharing this caller's
    /// shard declares or defines the same symbol with its own (correct) signature, two conflicting
    /// declarations of one extern "C" symbol in one translation unit is a hard compile error, not a
    /// harmless duplicate - confirmed the hard way for func_801C9BA0_statefree_v1 the first time this
    /// rewrite only touched the call site. Used instead of dropping a whole caller over one mismatched
    /// call site inside it - see the merge step in <see cref="EmitNsmbw"/>. Other state-free call sites
    /// in the same body, targeting other addresses, are left untouched.
    /// </summary>
    private static string DowngradeStateFreeCallSiteToSafePath(string body, uint targetAddress)
    {
        var prefix = $"if (MkwStateFreeAbiEnabled(0x{targetAddress:X8}u)";
        var search = 0;
        while ((search = body.IndexOf(prefix, search, StringComparison.Ordinal)) >= 0)
        {
            var open = body.IndexOf('{', search);
            if (open < 0) break;
            var trueClose = FindMatchingBrace(body, open);
            if (trueClose < 0) break;
            var cursor = trueClose + 1;
            while (cursor < body.Length && char.IsWhiteSpace(body[cursor])) ++cursor;
            if (!body.AsSpan(cursor).StartsWith("else", StringComparison.Ordinal))
            {
                search = trueClose + 1;
                continue;
            }
            cursor += "else".Length;
            while (cursor < body.Length && char.IsWhiteSpace(body[cursor])) ++cursor;
            if (cursor >= body.Length || body[cursor] != '{')
            {
                search = trueClose + 1;
                continue;
            }
            var falseOpen = cursor;
            var falseClose = FindMatchingBrace(body, falseOpen);
            if (falseClose < 0) break;
            var safeBody = body[(falseOpen + 1)..falseClose];
            body = body[..search] + safeBody + body[(falseClose + 1)..];
            search += safeBody.Length;
        }

        // Every call site targeting this address is now gone, so every forward declaration of one
        // of this address's _statefree symbols in this file is unreferenced dead weight - and, per
        // the mismatch that got us here, potentially a conflicting redeclaration if kept. Matched via
        // the same regex the mismatch detector uses so "what counts as this address's symbol" can
        // never drift between detection and cleanup.
        foreach (Match declaration in StateFreeSignatureRegex().Matches(body).Reverse())
        {
            if (!declaration.Groups["semi"].Success) continue; // a definition, not a forward declaration
            var symbol = declaration.Groups["sym"].Value;
            var symbolAddress = uint.Parse(
                symbol.AsSpan("func_".Length, 8),
                System.Globalization.NumberStyles.HexNumber,
                System.Globalization.CultureInfo.InvariantCulture);
            if (symbolAddress != targetAddress) continue;
            var lineStart = body.LastIndexOf('\n', declaration.Index) + 1;
            var lineEnd = body.IndexOf('\n', declaration.Index + declaration.Length);
            lineEnd = lineEnd < 0 ? body.Length : lineEnd + 1;
            body = body.Remove(lineStart, lineEnd - lineStart);
        }
        return body;

        static int FindMatchingBrace(string text, int open)
        {
            var depth = 0;
            for (var index = open; index < text.Length; ++index)
            {
                if (text[index] == '{') ++depth;
                else if (text[index] == '}' && --depth == 0) return index;
            }
            return -1;
        }
    }

    private static void WriteNsmbwCMakeManifest(
        string path,
        string outputRoot,
        int functionCount,
        IReadOnlyList<ShardInfo> shards,
        IReadOnlyList<string> registration)
    {
        var output = new StringBuilder();
        output.AppendLine("# Translator-owned stable shard graph; do not edit.");
        output.AppendLine("# NSMBW-specific: one combined graph for main.dol + all 4 RELs, since they");
        output.AppendLine("# are always linked together (no WiiCompiled-style profile/mod selection).");
        output.AppendLine($"set(NSMBW_TRANSLATED_SHARD_ROOT \"{CMakePath(outputRoot)}\")");
        output.AppendLine($"set(NSMBW_FUNCTION_COUNT {functionCount})");
        var ordered = shards
            .OrderByDescending(static shard => shard.CompileCostWeight)
            .ThenBy(static shard => shard.Fingerprint, StringComparer.Ordinal)
            .Select(static shard => shard.Path);
        AppendCMakeList(output, "NSMBW_SHARDS", ordered, preserveOrder: true);
        AppendCMakeList(output, "NSMBW_REGISTRATION_SOURCES", registration);
        WriteIfChanged(path, output.ToString());
    }
}
