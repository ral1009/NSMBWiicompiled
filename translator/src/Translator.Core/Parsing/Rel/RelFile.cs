using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Translator.Core.IO;
using Translator.Core.Loading;

namespace Translator.Core.Parsing.Rel;

/// <summary>
/// What a cross-module relocation needs to resolve against the *target* module instead of
/// the module currently being relocated: that module's own load address and its own section
/// table (file offsets are only meaningful relative to the section table they came from).
/// </summary>
public readonly record struct RelModuleInfo(uint BaseAddress, IReadOnlyList<RelSection> Sections);

/// <summary>Diagnostic record of one relocation that writes into a caller-specified address
/// range, for tracing exactly why a given memory location ends up with a given value.</summary>
public readonly record struct RelocationTrace(
    uint Dst, RelocationType Type, uint FromModuleId, byte SymbolSection, uint Addend, uint ComputedTarget);

public sealed class RelFile
{
    private readonly byte[] _raw;

    private RelFile(byte[] raw,
        uint moduleId,
        IReadOnlyList<RelSection> sections,
        IReadOnlyList<RelImportEntry> imports,
        uint totalSize)
    {
        _raw = raw;
        ModuleId = moduleId;
        Sections = sections;
        _imports = imports;
        _totalSize = totalSize;
    }

    private readonly IReadOnlyList<RelImportEntry> _imports;
    private readonly uint _totalSize;

    /// <summary>This REL's own module id (header offset 0x00) - what other RELs' import tables use to refer to it.</summary>
    public uint ModuleId { get; }

    /// <summary>The only REL header surface any caller outside this file reads.</summary>
    public IReadOnlyList<RelSection> Sections { get; }

    public static RelFile Load(string path)
    {
        var raw = File.ReadAllBytes(path);
        using var stream = new MemoryStream(raw, writable: false);
        using var reader = new BigEndianBinaryReader(stream, leaveOpen: true);

        reader.Seek(0x00, SeekOrigin.Begin);
        var moduleId = reader.ReadUInt32();

        reader.Seek(0x0C, SeekOrigin.Begin);
        var sectionCount = reader.ReadUInt32();
        var sectionTableOffset = reader.ReadUInt32();

        reader.Seek(0x1C, SeekOrigin.Begin);
        _ = reader.ReadUInt32(); // version (unused for now)
        var bssSize = reader.ReadUInt32();

        reader.Seek(0x28, SeekOrigin.Begin);
        var importTableOffset = reader.ReadUInt32();
        var importTableSize = reader.ReadUInt32();

        _ = reader.ReadByte(); // prolog section (0x30)
        _ = reader.ReadByte(); // epilog section (0x31)
        _ = reader.ReadByte(); // unresolved section (0x32)
        var bssSectionIndex = reader.ReadByte(); // 0x33

        // Offsets used by the runtime loader (kept for debugging / layout checks)
        _ = reader.ReadUInt32(); // prolog offset within prolog section (0x34)
        _ = reader.ReadUInt32(); // epilog offset within epilog section (0x38)
        _ = reader.ReadUInt32(); // unresolved offset within unresolved section (0x3C)

        _ = reader.ReadUInt32(); // align (0x40)
        var bssAlign = reader.ReadUInt32(); // 0x44

        // Basic validation
        if (sectionTableOffset + sectionCount * 8 > raw.Length)
        {
            throw new InvalidDataException("Section table extends beyond REL bounds");
        }

        if (importTableOffset + importTableSize > raw.Length)
        {
            throw new InvalidDataException("Import table extends beyond REL bounds");
        }

        var sections = ParseSections(raw, sectionTableOffset, sectionCount);
        if (bssAlign == 0)
        {
            bssAlign = 0x20;
        }
        // BSS must be placed right after the real resident image, not the raw file. The on-disc
        // .rel is bigger than what ends up in RAM: c_dylink.cpp's do_link() (NSMBW-Decomp) reads
        // the whole file, calls OSLink, then shrinks the heap block to fixSize+bssSize, discarding
        // the relocation/import tables (relOffset/impOffset/impSize) that trail the section data -
        // those exist only on disc to drive linking, never get mapped into guest memory. Using
        // raw.Length here (as this used to) means BSS - and therefore this REL's total resident
        // size - included that discarded tail, inflating every REL's memory footprint far beyond
        // its real one. With 4 RELs placed back-to-back at their real observed addresses, that
        // inflation is exactly what made each one's computed range run into the next REL's start.
        var maxSectionExtent = ComputeMaxSectionExtent(sections, bssSectionIndex);
        var bssOffset = AlignUp(maxSectionExtent, bssAlign);
        var totalSize = checked(bssOffset + bssSize);
        var adjustedSections = ApplyBssLayout(sections, bssSectionIndex, bssOffset, bssSize);
        var imports = ParseImports(raw, importTableOffset, importTableSize);

        return new RelFile(
            raw,
            moduleId,
            new ReadOnlyCollection<RelSection>(adjustedSections),
            new ReadOnlyCollection<RelImportEntry>(imports),
            totalSize);
    }

    private static List<RelSection> ParseSections(byte[] raw, uint offset, uint count)
    {
        var sections = new List<RelSection>((int)count);
        for (var i = 0; i < count; i++)
        {
            var entryOffset = checked((int)(offset + (uint)(i * 8)));
            var offRaw = ReadUInt32(raw, entryOffset);
            var size = ReadUInt32(raw, entryOffset + 4);
            var executable = (offRaw & 1) != 0;
            var fileOffset = offRaw & ~1u;
            sections.Add(new RelSection(i, fileOffset, size, executable));
        }
        return sections;
    }

    private static List<RelSection> ApplyBssLayout(List<RelSection> sections, byte bssSectionIndex, uint bssOffset, uint bssSize)
    {
        // REL BSS sections have fileOffset==0; the runtime loader fills the real address in at load
        // time. We place BSS right after the loaded REL bytes in our flat image so relocations that
        // target or write into BSS resolve in-bounds.
        var bssCursor = bssOffset;
        var bssEnd = checked(bssOffset + bssSize);

        var adjusted = new List<RelSection>(sections.Count);
        foreach (var section in sections)
        {
            var isBss = section.FileOffset == 0 && (section.Size != 0 || section.Index == bssSectionIndex);
            if (!isBss)
            {
                adjusted.Add(section);
                continue;
            }

            if (bssCursor > bssEnd)
            {
                throw new InvalidDataException("BSS layout exceeded declared BSS size");
            }

            adjusted.Add(new RelSection(section.Index, bssCursor, section.Size, section.Executable));
            bssCursor = checked(bssCursor + section.Size);
        }

        if (bssCursor > bssEnd)
        {
            throw new InvalidDataException($"BSS section sizes exceed header bssSize (cursor=0x{bssCursor:X8}, end=0x{bssEnd:X8})");
        }

        return adjusted;
    }

    private static List<RelImportEntry> ParseImports(byte[] raw, uint offset, uint size)
    {
        var imports = new List<RelImportEntry>((int)(size / 8));
        for (var cursor = offset; cursor < offset + size; cursor += 8)
        {
            var moduleId = ReadUInt32(raw, (int)cursor);
            var relocationOffset = ReadUInt32(raw, (int)cursor + 4);
            imports.Add(new RelImportEntry(moduleId, relocationOffset));
        }
        return imports;
    }

    /// <summary>
    /// The real resident image ends at the last byte of the last actual (non-BSS) section - not
    /// at the end of the file, which also contains the relocation/import tables. Mirrors
    /// ApplyBssLayout's own "is this section BSS" check so both agree on what counts as real data.
    /// </summary>
    private static uint ComputeMaxSectionExtent(List<RelSection> sections, byte bssSectionIndex)
    {
        uint max = 0;
        foreach (var section in sections)
        {
            var isBss = section.FileOffset == 0 && (section.Size != 0 || section.Index == bssSectionIndex);
            if (isBss)
            {
                continue;
            }
            max = checked(Math.Max(max, section.FileOffset + section.Size));
        }
        return max;
    }

    private static uint AlignUp(uint value, uint alignment)
    {
        if (alignment == 0)
        {
            return value;
        }

        if ((alignment & (alignment - 1)) != 0)
        {
            throw new InvalidDataException($"Alignment must be a power of two (got 0x{alignment:X})");
        }

        var mask = alignment - 1;
        return checked((value + mask) & ~mask);
    }

    public RelImage BuildImage(uint baseAddress,
        uint dolBaseAddress = MemoryLayout.DolBaseAddress,
        bool applyRelocations = true,
        IReadOnlyDictionary<uint, RelModuleInfo>? moduleRegistry = null)
    {
        var buffer = new byte[checked((int)_totalSize)];
        // _totalSize now excludes the on-disc relocation/import table tail (see ComputeMaxSectionExtent),
        // so it can be smaller than _raw.Length - copy only what fits, i.e. the real section data.
        var copyLength = Math.Min(_raw.Length, buffer.Length);
        Buffer.BlockCopy(_raw, 0, buffer, 0, copyLength);

        if (applyRelocations)
        {
            ApplyRelocations(buffer, baseAddress, dolBaseAddress, moduleRegistry);
        }

        return new RelImage(buffer, baseAddress);
    }

    private void ApplyRelocations(byte[] memory, uint baseAddress, uint dolBaseAddress,
        IReadOnlyDictionary<uint, RelModuleInfo>? moduleRegistry)
    {
        foreach (var import in _imports)
        {
            uint currentOffset = 0;
            byte currentSection = 0;
            var cursor = import.RelocationOffset;

            while (true)
            {
                var delta = ReadUInt16(_raw, (int)cursor);
                var type = (RelocationType)_raw[cursor + 2];
                var symbolSection = _raw[cursor + 3];
                var addend = ReadUInt32(_raw, (int)cursor + 4);
                cursor += 8;

                if (type == RelocationType.R_RVL_STOP)
                {
                    break;
                }

                if (type == RelocationType.R_RVL_SECT)
                {
                    currentSection = symbolSection;
                    currentOffset = 0;
                    continue;
                }

                currentOffset = checked(currentOffset + delta);
                if (currentSection >= Sections.Count)
                {
                    throw new InvalidDataException($"Relocation referenced unknown section index {currentSection}");
                }

                var dstSection = Sections[currentSection];
                var dst = baseAddress + dstSection.FileOffset + currentOffset;

                uint target;
                if (import.ModuleId == 0)
                {
                    target = addend;
                    if (target < MemoryLayout.RamBase)
                    {
                        target = dolBaseAddress + target;
                    }
                }
                else if (import.ModuleId == ModuleId)
                {
                    // Self-relocation: the symbol lives in *this* REL, so this module's own
                    // base address and section table are the right ones to resolve against.
                    if (symbolSection >= Sections.Count)
                    {
                        throw new InvalidDataException($"Relocation referenced unknown symbol section {symbolSection}");
                    }

                    var symbol = Sections[symbolSection];
                    target = baseAddress + symbol.FileOffset + addend;
                }
                else
                {
                    // Cross-module relocation: the symbol lives in a *different* REL. Its file
                    // offsets are only meaningful relative to that module's own load address and
                    // section table - reusing this module's baseAddress/Sections here (as earlier
                    // code did) silently computes a bogus small-looking "address" instead of the
                    // real target, since the two modules' section tables don't correspond.
                    if (moduleRegistry is null || !moduleRegistry.TryGetValue(import.ModuleId, out var otherModule))
                    {
                        throw new InvalidDataException(
                            $"Relocation at dst=0x{dst:X8} imports from module {import.ModuleId}, but no module " +
                            "registry entry was supplied for it (pass moduleRegistry to BuildImage covering every " +
                            "linked REL for a multi-module product).");
                    }

                    if (symbolSection >= otherModule.Sections.Count)
                    {
                        throw new InvalidDataException(
                            $"Relocation referenced unknown symbol section {symbolSection} in module {import.ModuleId}");
                    }

                    var symbol = otherModule.Sections[symbolSection];
                    target = otherModule.BaseAddress + symbol.FileOffset + addend;
                }

                if (target < MemoryLayout.RamBase
                    && type is RelocationType.R_PPC_ADDR32 or RelocationType.R_PPC_ADDR16_LO
                        or RelocationType.R_PPC_ADDR16_HA or RelocationType.R_PPC_REL24)
                {
                    Console.Error.WriteLine(
                        $"[relfile] WARNING: relocation produced out-of-range target 0x{target:X8} " +
                        $"(< RAM base 0x{MemoryLayout.RamBase:X8}) at dst=0x{dst:X8}, " +
                        $"importingModule={ModuleId} fromModule={import.ModuleId} type={type} " +
                        $"symbolSection={symbolSection} addend=0x{addend:X8}");
                }

                var memIndex = checked((int)(dst - baseAddress));
                var orig = ReadUInt32(memory, memIndex);

                switch (type)
                {
                    // StaticR.rel uses exactly these five relocation types; any
                    // other encoding reaches the default arm and fails loudly.
                    case RelocationType.R_RVL_NONE:
                        break;
                    case RelocationType.R_PPC_ADDR32:
                        WriteUInt32(memory, memIndex, target);
                        break;
                    case RelocationType.R_PPC_ADDR16_LO:
                        WriteUInt16(memory, memIndex, (ushort)(target & 0xFFFF));
                        break;
                    case RelocationType.R_PPC_ADDR16_HA:
                        WriteUInt16(memory, memIndex, (ushort)(((target + 0x8000) >> 16) & 0xFFFF));
                        break;
                    case RelocationType.R_PPC_REL24:
                        {
                            var disp = (target - dst) & 0x03FFFFFC;
                            var patched = (orig & 0xFC000003) | disp;
                            WriteUInt32(memory, memIndex, patched);
                            break;
                        }
                    default:
                        throw new NotSupportedException($"Unhandled relocation type {type} at 0x{dst:X8}");
                }
            }
        }
    }

    /// <summary>
    /// Walks every relocation exactly like ApplyRelocations, but instead of writing memory,
    /// reports the ones whose write address (dst) falls in [rangeStart, rangeEndExclusive) -
    /// useful for tracing exactly which import/relocation produced a suspicious value at a
    /// known-bad address, using the same resolution logic (including the module registry) that
    /// actually runs at build time, instead of a separately hand-rolled parser.
    /// </summary>
    public List<RelocationTrace> TraceRelocationsInRange(uint baseAddress, uint dolBaseAddress,
        IReadOnlyDictionary<uint, RelModuleInfo>? moduleRegistry, uint rangeStart, uint rangeEndExclusive)
    {
        var results = new List<RelocationTrace>();
        foreach (var import in _imports)
        {
            uint currentOffset = 0;
            byte currentSection = 0;
            var cursor = import.RelocationOffset;

            while (true)
            {
                var delta = ReadUInt16(_raw, (int)cursor);
                var type = (RelocationType)_raw[cursor + 2];
                var symbolSection = _raw[cursor + 3];
                var addend = ReadUInt32(_raw, (int)cursor + 4);
                cursor += 8;

                if (type == RelocationType.R_RVL_STOP)
                {
                    break;
                }

                if (type == RelocationType.R_RVL_SECT)
                {
                    currentSection = symbolSection;
                    currentOffset = 0;
                    continue;
                }

                currentOffset = checked(currentOffset + delta);
                if (currentSection >= Sections.Count)
                {
                    throw new InvalidDataException($"Relocation referenced unknown section index {currentSection}");
                }

                var dstSection = Sections[currentSection];
                var dst = baseAddress + dstSection.FileOffset + currentOffset;

                if (dst < rangeStart || dst >= rangeEndExclusive)
                {
                    continue;
                }

                uint target;
                if (import.ModuleId == 0)
                {
                    target = addend;
                    if (target < MemoryLayout.RamBase)
                    {
                        target = dolBaseAddress + target;
                    }
                }
                else if (import.ModuleId == ModuleId && symbolSection < Sections.Count)
                {
                    var symbol = Sections[symbolSection];
                    target = baseAddress + symbol.FileOffset + addend;
                }
                else if (moduleRegistry is not null && moduleRegistry.TryGetValue(import.ModuleId, out var otherModule)
                         && symbolSection < otherModule.Sections.Count)
                {
                    var symbol = otherModule.Sections[symbolSection];
                    target = otherModule.BaseAddress + symbol.FileOffset + addend;
                }
                else
                {
                    target = 0xFFFFFFFFu; // unresolvable with the registry given - flagged, not thrown, for tracing
                }

                results.Add(new RelocationTrace(dst, type, import.ModuleId, symbolSection, addend, target));
            }
        }
        return results;
    }

    private static ushort ReadUInt16(IReadOnlyList<byte> data, int offset)
    {
        if (offset + 2 > data.Count)
        {
            throw new InvalidDataException("Attempted to read past end of REL payload");
        }
        Span<byte> buffer = stackalloc byte[2];
        buffer[0] = data[offset];
        buffer[1] = data[offset + 1];
        return BinaryPrimitives.ReadUInt16BigEndian(buffer);
    }

    private static uint ReadUInt32(IReadOnlyList<byte> data, int offset)
    {
        if (offset + 4 > data.Count)
        {
            throw new InvalidDataException("Attempted to read past end of REL payload");
        }
        Span<byte> buffer = stackalloc byte[4];
        buffer[0] = data[offset];
        buffer[1] = data[offset + 1];
        buffer[2] = data[offset + 2];
        buffer[3] = data[offset + 3];
        return BinaryPrimitives.ReadUInt32BigEndian(buffer);
    }

    private static void WriteUInt16(IList<byte> data, int offset, ushort value)
    {
        if (offset < 0 || offset + 2 > data.Count)
        {
            throw new InvalidDataException("Attempted to write past end of REL memory");
        }
        Span<byte> buffer = stackalloc byte[2];
        BinaryPrimitives.WriteUInt16BigEndian(buffer, value);
        data[offset] = buffer[0];
        data[offset + 1] = buffer[1];
    }

    private static void WriteUInt32(IList<byte> data, int offset, uint value)
    {
        if (offset < 0 || offset + 4 > data.Count)
        {
            throw new InvalidDataException("Attempted to write past end of REL memory");
        }
        Span<byte> buffer = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(buffer, value);
        data[offset] = buffer[0];
        data[offset + 1] = buffer[1];
        data[offset + 2] = buffer[2];
        data[offset + 3] = buffer[3];
    }
}
