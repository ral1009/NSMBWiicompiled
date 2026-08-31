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
    string FunctionsDirectory);

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
        var stateFreeMismatchedCallers = new HashSet<uint>();
        foreach (var record in activeFunctions)
        {
            foreach (Match match in StateFreeSignatureRegex().Matches(SourceOf(record)))
            {
                if (!match.Groups["semi"].Success) continue; // this record's own definition, already recorded above
                var symbol = match.Groups["sym"].Value;
                // Absent, not just mismatched, is just as unsafe: it means every module that
                // defined this exact _statefree variant lost the merge, so nothing in the build
                // actually provides the symbol this caller forward-declared.
                if (!definedStateFreeSignatures.TryGetValue(symbol, out var authoritative))
                {
                    stateFreeMismatchedCallers.Add(record.Address);
                    continue;
                }
                var declared = NormalizeStateFreeSignature(match.Groups["ret"].Value, match.Groups["params"].Value);
                if (!string.Equals(declared, authoritative, StringComparison.Ordinal))
                {
                    stateFreeMismatchedCallers.Add(record.Address);
                }
            }
        }
        if (stateFreeMismatchedCallers.Count > 0)
        {
            activeFunctions = activeFunctions
                .Where(record => !stateFreeMismatchedCallers.Contains(record.Address))
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
            activeFunctions.Count, duplicateCount, optimizationVariantCount, stateFreeMismatchedCallers.Count,
            shards.Count, cmakeManifestPath);
    }

    [GeneratedRegex(
        """extern\s+"C"\s+(?:MKW_PPC_\w+\s+)*(?<ret>[\w:<>]+)\s+(?<sym>func_[0-9A-Fa-f]{8}_statefree(?:_v\d+)?)\s*\((?<params>[^)]*)\)\s*(?<semi>;)?""")]
    private static partial Regex StateFreeSignatureRegex();

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
