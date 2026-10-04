// ps5aio-helper — JSON-CLI для PS5 Combine AIO поверх библиотек PS5 PKG Tool (pearlxcore, GPL-3.0).
//
//   scan          --folder D [--folder D2] [--no-recursive]          NDJSON: progress / game / error / done
//   details       --game-file G.json --out DIR                       пишет DIR\details.json + картинки (icon0, pic0..2, trophy_N)
//   files         --game-file G.json --out FILE.json                 список файлов внутри образа/пакета/дампа
//   extract       --game-file G.json (--path REL ... | --paths-file LIST.txt) --out DIR   извлечь файлы/папки (структура сохраняется)
//   compare       --game-a A.json --game-b B.json --out REPORT.json [--deep]   сверка двух образов/дампов/пакетов
//   verify        <image>                                            проверка образа exFAT / FFPKG (UFS2) / FFPFSC
//   repair        <image.exfat>                                      ремонт exFAT из валидной загрузочной области
//   refresh-ampr  <image.exfat>                                      пересоздать индекс AMPR (ampr_emu.index)
//   rebuild       <image.ffpkg>                                      пересобрать метаданные FFPKG (UFS2) с полной проверкой
//   edit          <image> [--replace IMG SRC] [--add IMG SRC] [--add-tree IMG SRCDIR] [--mkdir IMG] [--delete IMG]   (exFAT и FFPKG)
//   convert       <source> <output> --to exfat|ffpkg|ffpfsc [--overwrite]
//
// Строки прогресса имеют вид "[####------]  45.0% · этап" — их разбирает ToolRunner в GUI.

using System.Text;
using System.Text.Json;
using PS5PKGTool.Core.Models;
using PS5PKGTool.Core.Services;
using PS5PKGTool.Ffpfsc;
using UFS2Tool;

namespace Ps5AioHelper;

internal static class Program
{
    private static readonly object OutLock = new();
    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true };

    private static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            Console.WriteLine("ps5aio-helper: scan | details | files | extract | compare | verify | repair | refresh-ampr | rebuild | edit | convert");
            return args.Length == 0 ? 2 : 0;
        }

        using var cts = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };
        var opts = Options.Parse(args.Skip(1));
        try
        {
            switch (args[0].ToLowerInvariant())
            {
                case "scan": return await ScanAsync(opts, cts.Token);
                case "details": return await DetailsAsync(opts, cts.Token);
                case "files": return FilesList(opts, cts.Token);
                case "extract": return await ExtractAsync(opts, cts.Token);
                case "compare": return await CompareAsync(opts, cts.Token);
                case "verify": return await VerifyImageAsync(opts, cts.Token);
                case "rebuild": return await RebuildAsync(opts, cts.Token);
                case "repair": return await RepairAsync(opts, cts.Token);
                case "refresh-ampr": return await RefreshAmprAsync(opts, cts.Token);
                case "edit": return await EditAsync(opts, cts.Token);
                case "convert": return await ConvertAsync(opts, cts.Token);
                default:
                    Console.Error.WriteLine("ERROR: unknown command " + args[0]);
                    return 2;
            }
        }
        catch (OperationCanceledException)
        {
            Console.Error.WriteLine("ERROR: cancelled");
            return 130;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR: " + ex.Message);
            return 1;
        }
    }

    // ---------------------------------------------------------------- вывод

    private static void Emit(object value)
    {
        string line = JsonSerializer.Serialize(value);
        lock (OutLock) Console.Out.WriteLine(line);
    }

    private static readonly System.Diagnostics.Stopwatch BarClock = System.Diagnostics.Stopwatch.StartNew();
    private static long _lastBarMs = -1000;

    // Не чаще 4 раз в секунду (иначе на 46 ГБ получились бы десятки тысяч строк); финальное значение — всегда.
    private static void Bar(double fraction, string text)
    {
        fraction = Math.Clamp(fraction, 0, 1);
        long now = BarClock.ElapsedMilliseconds;
        if (fraction < 1 && now - _lastBarMs < 250) return;
        _lastBarMs = now;
        int filled = (int)Math.Round(fraction * 24);
        string bar = new string('#', filled) + new string('-', 24 - filled);
        lock (OutLock) Console.Out.WriteLine($"[{bar}] {fraction * 100:0.0}% · {text}");
    }

    private sealed class Sync<T>(Action<T> action) : IProgress<T>
    {
        public void Report(T value) => action(value);
    }

    private static Ps5GameInfo LoadGameFrom(string file) =>
        JsonSerializer.Deserialize<Ps5GameInfo>(File.ReadAllText(file), Json)
        ?? throw new InvalidDataException("Cannot read game description: " + file);

    private static Ps5GameInfo LoadGame(Options o) => LoadGameFrom(o.Require("game-file"));

    // ---------------------------------------------------------------- scan

    private static async Task<int> ScanAsync(Options o, CancellationToken ct)
    {
        List<string> folders = o.All("folder");
        if (folders.Count == 0) throw new ArgumentException("--folder is required");
        bool recursive = !o.Has("no-recursive");

        var progress = new Sync<Ps5ScanProgress>(p =>
            Emit(new { type = "progress", processed = p.Processed, total = p.Total, path = p.CurrentPath }));
        Ps5ScanResult result = await new Ps5LibraryScanner().ScanAsync(folders, recursive, null, progress, ct);

        foreach (string error in result.Errors) Emit(new { type = "error", message = error });
        foreach (Ps5GameInfo game in result.Games) Emit(new { type = "game", game });
        Emit(new { type = "done", count = result.Games.Count });
        return 0;
    }

    // ---------------------------------------------------------------- details

    private static async Task<int> DetailsAsync(Options o, CancellationToken ct)
    {
        Ps5GameInfo game = LoadGame(o);
        string outDir = o.Require("out");
        Directory.CreateDirectory(outDir);

        Ps5GameDetails d = await new Ps5DetailsLoader().LoadAsync(game, ct);

        string icon = SaveImage(d.Icon, Path.Combine(outDir, "icon0.png"));
        string pic0 = SaveImage(d.Background, Path.Combine(outDir, "pic0.png"));
        string pic1 = SaveImage(d.Background1, Path.Combine(outDir, "pic1.png"));
        string pic2 = SaveImage(d.Background2, Path.Combine(outDir, "pic2.png"));

        object? trophies = null;
        if (d.TrophySet is { } set)
        {
            var items = new List<object>();
            foreach (Ps5Trophy t in set.Trophies)
            {
                string iconName = "";
                string target = Path.Combine(outDir, $"trophy_{t.Id}.png");
                if (t.IconPng is { Length: > 0 } png) { File.WriteAllBytes(target, png); iconName = Path.GetFileName(target); }
                else if (t.Icon is not null) iconName = SaveImage(t.Icon, target);
                items.Add(new
                {
                    id = t.Id, grade = t.Grade, hidden = t.Hidden, hasReward = t.HasReward, name = t.Name,
                    description = t.Description, platinumId = t.PlatinumTrophyId, unlock = t.UnlockCondition, icon = iconName
                });
            }
            trophies = new
            {
                npCommunicationId = set.NpCommunicationId, title = set.Title, version = set.TrophySetVersion,
                language = set.SelectedLanguage, languages = set.Languages, integrityValid = set.IntegrityValid, items
            };
        }

        object? uds = null;
        if (d.Uds is { } u)
        {
            uds = new
            {
                npCommunicationId = u.NpCommunicationId, enumGroupCount = u.EnumGroupCount, eventCount = u.EventCount,
                statCount = u.StatCount, ruleCount = u.ExtractionRuleCount, integrityValid = u.IntegrityValid,
                events = u.Events.Select(e => new { name = e.Name, type = e.Type, group = e.DefinitionGroup, propertyCount = e.PropertyCount }),
                stats = u.Stats.Select(s => new
                {
                    id = s.StatId, name = s.Name, group = s.DefinitionGroup, origin = s.Origin, dataType = s.DataType,
                    aggregation = s.Aggregation, min = s.MinValue, max = s.MaxValue, initial = s.InitialValue
                }),
                enums = u.EnumGroups.Select(g => new { id = g.EnumId, group = g.DefinitionGroup, values = g.Values }),
                rules = u.Rules.Select(r => new
                {
                    id = r.RuleId, group = r.DefinitionGroup, eventName = r.EventName, condition = r.Condition, input = r.Input,
                    convert = r.Convert, outputStatId = r.OutputStatId, outputStat = r.OutputStatName
                })
            };
        }

        object? exe = null;
        if (d.Executable is { } x)
        {
            exe = new
            {
                isSelf = x.IsSelf, selfMagic = x.SelfMagic, fileSize = x.FileSize, machine = x.Machine,
                entryPoint = "0x" + x.EntryPoint.ToString("X"), programHeaders = x.ProgramHeaderCount,
                sectionHeaders = x.SectionHeaderCount, selfSegments = x.SelfSegmentCount,
                modules = x.Modules.Select(m => new { name = m.Name, path = m.RelativePath, size = m.Size, kind = m.Kind })
            };
        }

        var sections = d.Sections.ToDictionary(kv => kv.Key,
            kv => new { state = kv.Value.State.ToString(), origin = kv.Value.Origin, message = kv.Value.Message });

        var document = new
        {
            sections, errors = d.Errors, trophies, uds, executable = exe,
            files = new { totalSize = d.Files.TotalSize, fileCount = d.Files.FileCount },
            artwork = new { icon, pic0, pic1, pic2 }
        };
        string detailsPath = Path.Combine(outDir, "details.json");
        File.WriteAllText(detailsPath, JsonSerializer.Serialize(document), new UTF8Encoding(false));
        Emit(new { type = "result", file = detailsPath });
        return 0;
    }

    // ---------------------------------------------------------------- files / extract

    private static int FilesList(Options o, CancellationToken ct)
    {
        Ps5GameInfo game = LoadGame(o);
        string outFile = o.Require("out");
        using IReadOnlyGameFileSystem fs = GameFileSystem.Open(game, ct);
        var list = fs.Files.Select(f => new { p = f.RelativePath, s = f.Size, o = f.Origin }).ToList();
        File.WriteAllText(outFile, JsonSerializer.Serialize(list), new UTF8Encoding(false));
        Emit(new { type = "result", file = outFile, count = list.Count });
        return 0;
    }

    private static async Task<int> ExtractAsync(Options o, CancellationToken ct)
    {
        Ps5GameInfo game = LoadGame(o);
        string outDir = Path.GetFullPath(o.Require("out"));
        // Пути можно передать и списком в файле (одна строка = один путь): у выделенной папки их могут быть тысячи.
        var requested = o.All("path");
        foreach (string listFile in o.All("paths-file"))
            requested.AddRange(File.ReadAllLines(listFile, Encoding.UTF8).Where(l => !string.IsNullOrWhiteSpace(l)));
        List<string> wanted = requested.Select(GameFileSystem.NormalizePath).ToList();
        if (wanted.Count == 0) throw new ArgumentException("--path or --paths-file is required");

        using IReadOnlyGameFileSystem fs = GameFileSystem.Open(game, ct);
        // Выбор может быть файлом или папкой: берём всё, что совпадает или лежит внутри.
        var selected = fs.Files.Where(f =>
        {
            string rel = GameFileSystem.NormalizePath(f.RelativePath);
            return wanted.Any(w => rel.Equals(w, StringComparison.OrdinalIgnoreCase) ||
                                   rel.StartsWith(w + "/", StringComparison.OrdinalIgnoreCase));
        }).ToList();
        if (selected.Count == 0) throw new FileNotFoundException("Nothing matches the selected path(s).");

        long total = selected.Sum(f => f.Size);
        long done = 0;
        byte[] buffer = new byte[1024 * 1024];
        foreach (GameFileRecord rec in selected)
        {
            ct.ThrowIfCancellationRequested();
            string rel = GameFileSystem.NormalizePath(rec.RelativePath);
            string target = Path.GetFullPath(Path.Combine(outDir, rel.Replace('/', Path.DirectorySeparatorChar)));
            if (!target.StartsWith(outDir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Unsafe path in image: " + rel);          // защита от «../» внутри образа
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);

            string temp = target + ".part";
            await using (Stream input = fs.OpenRead(rel))
            await using (var output = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 1024, FileOptions.Asynchronous))
            {
                while (true)
                {
                    int read = await input.ReadAsync(buffer, ct);
                    if (read == 0) break;
                    await output.WriteAsync(buffer.AsMemory(0, read), ct);
                    done += read;
                    Bar(total == 0 ? 1 : (double)done / total, rel);
                }
            }
            File.Move(temp, target, true);
        }
        Bar(1, $"{selected.Count} file(s)");
        Console.WriteLine($"Extracted {selected.Count} file(s) to {outDir}");
        return 0;
    }

    // ---------------------------------------------------------------- сверка двух образов

    private sealed record Entry(string Stripped, string Original, long Size);

    private static string Norm(string p) => GameFileSystem.NormalizePath(p);

    // eboot.bin ближайший к корню: по нему определяем, нет ли лишней вложенной папки («game/eboot.bin»).
    private static string FindEboot(IEnumerable<string> paths)
    {
        string best = "";
        int bestDepth = int.MaxValue;
        foreach (string p in paths)
        {
            if (!p.EndsWith("eboot.bin", StringComparison.OrdinalIgnoreCase)) continue;
            if (p.Length > 9 && p[^10] != '/') continue;
            int depth = p.Count(c => c == '/');
            if (depth < bestDepth) { best = p; bestDepth = depth; }
        }
        return best;
    }

    private static Dictionary<string, Entry> IndexFiles(IReadOnlyList<GameFileRecord> files, string prefix)
    {
        var map = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);
        foreach (GameFileRecord f in files)
        {
            string original = Norm(f.RelativePath);
            string stripped = prefix.Length > 0 && original.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                ? original[prefix.Length..] : original;
            map.TryAdd(stripped, new Entry(stripped, original, f.Size));
        }
        return map;
    }

    private static void WalkJson(JsonElement e, string path, Dictionary<string, string> into)
    {
        switch (e.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (JsonProperty prop in e.EnumerateObject())
                    WalkJson(prop.Value, path.Length == 0 ? prop.Name : path + "." + prop.Name, into);
                break;
            case JsonValueKind.Array:
                int i = 0;
                foreach (JsonElement x in e.EnumerateArray()) WalkJson(x, $"{path}[{i++}]", into);
                break;
            default:
                into[path] = e.ToString();
                break;
        }
    }

    private static Dictionary<string, string> FlattenParam(string raw)
    {
        var d = new Dictionary<string, string>(StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(raw)) return d;
        try { using JsonDocument doc = JsonDocument.Parse(raw); WalkJson(doc.RootElement, "", d); }
        catch (JsonException) { /* param.json не разобрался — просто без сравнения полей */ }
        return d;
    }

    private static async Task<int> FillAsync(Stream s, byte[] buffer, CancellationToken ct)
    {
        int total = 0;
        while (total < buffer.Length)
        {
            int n = await s.ReadAsync(buffer.AsMemory(total, buffer.Length - total), ct);
            if (n == 0) break;
            total += n;
        }
        return total;
    }

    // Побайтовое сравнение двух потоков; -1 = одинаковы, иначе смещение первого отличия.
    private static async Task<long> FirstDifferenceAsync(Stream a, Stream b, Action<long> progress, CancellationToken ct)
    {
        byte[] ba = new byte[1024 * 1024], bb = new byte[1024 * 1024];
        long offset = 0;
        while (true)
        {
            int na = await FillAsync(a, ba, ct), nb = await FillAsync(b, bb, ct);
            int common = Math.Min(na, nb);
            int diff = ba.AsSpan(0, common).CommonPrefixLength(bb.AsSpan(0, common));
            if (diff < common) return offset + diff;
            if (na != nb) return offset + common;
            if (na == 0) return -1;
            offset += na;
            progress(na);
        }
    }

    // Параметры самого exFAT из загрузочного сектора (спецификация exFAT, Main Boot Sector). Образ .exfat у PS5 — «сырой»
    // том с загрузочным сектором в начале файла; если это не так или формат другой — возвращаем null.
    private static object? ExfatGeometry(Ps5GameInfo g)
    {
        if (g.SourceKind != Ps5SourceKind.FilesystemImage || !File.Exists(g.RootPath)) return null;
        try
        {
            using var fs = new FileStream(g.RootPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            byte[] b = new byte[512];
            if (fs.Read(b, 0, 512) < 512 || Encoding.ASCII.GetString(b, 3, 8) != "EXFAT   ") return null;
            long sector = 1L << b[108];
            long cluster = sector << b[109];
            return new
            {
                clusterSize = cluster, bytesPerSector = sector,
                volumeLength = (long)BitConverter.ToUInt64(b, 72) * sector,
                fatOffset = (long)BitConverter.ToUInt32(b, 80), fatLength = (long)BitConverter.ToUInt32(b, 84),
                clusterHeapOffset = (long)BitConverter.ToUInt32(b, 88), clusterCount = (long)BitConverter.ToUInt32(b, 92),
                rootCluster = (long)BitConverter.ToUInt32(b, 96), serial = BitConverter.ToUInt32(b, 100).ToString("X8"),
                revision = BitConverter.ToUInt16(b, 104).ToString("X4"), volumeFlags = (long)BitConverter.ToUInt16(b, 106),
                fats = (long)b[110], percentInUse = (long)b[112], fileLength = fs.Length
            };
        }
        catch (IOException) { return null; }
    }

    private static object SideInfo(Ps5GameInfo g, int count, long total, string eboot, string prefix) => new
    {
        exfat = ExfatGeometry(g),
        title = g.Title, titleId = g.TitleId, contentId = g.ContentId, format = g.SourceDescription, path = g.RootPath,
        contentVersion = g.ContentVersion, masterVersion = g.MasterVersion, requiredFw = g.RequiredSystemSoftware,
        sdk = g.SdkVersion, containerLength = g.ContainerFileLength, fileCount = count, totalSize = total,
        eboot, rootPrefix = prefix
    };

    private static async Task<int> CompareAsync(Options o, CancellationToken ct)
    {
        Ps5GameInfo ga = LoadGameFrom(o.Require("game-a")), gb = LoadGameFrom(o.Require("game-b"));
        string outFile = o.Require("out");
        bool deep = o.Has("deep");
        const long SmallLimit = 16L * 1024 * 1024;       // без --deep побайтово сверяются только файлы до 16 МБ

        using IReadOnlyGameFileSystem fa = GameFileSystem.Open(ga, ct);
        using IReadOnlyGameFileSystem fb = GameFileSystem.Open(gb, ct);

        string ebootA = FindEboot(fa.Files.Select(f => Norm(f.RelativePath)));
        string ebootB = FindEboot(fb.Files.Select(f => Norm(f.RelativePath)));
        string prefA = ebootA.Length >= 9 ? ebootA[..^9] : "";
        string prefB = ebootB.Length >= 9 ? ebootB[..^9] : "";
        var ia = IndexFiles(fa.Files, prefA);
        var ib = IndexFiles(fb.Files, prefB);

        var onlyA = new List<object>();
        var onlyB = new List<object>();
        var caseDiff = new List<object>();
        var differ = new List<object>();
        var toCompare = new List<(Entry A, Entry B)>();
        int sameBySizeOnly = 0, sameVerified = 0;

        foreach (var (key, ea) in ia.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
        {
            if (!ib.TryGetValue(key, out Entry? eb)) { onlyA.Add(new { p = ea.Original, s = ea.Size }); continue; }
            if (!string.Equals(ea.Stripped, eb.Stripped, StringComparison.Ordinal))
                caseDiff.Add(new { a = ea.Stripped, b = eb.Stripped });
            if (ea.Size != eb.Size) { differ.Add(new { p = ea.Stripped, sa = ea.Size, sb = eb.Size, why = "size", offset = -1L }); continue; }
            if (deep || ea.Size <= SmallLimit) toCompare.Add((ea, eb)); else sameBySizeOnly++;
        }
        foreach (var (key, eb) in ib.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
            if (!ia.ContainsKey(key)) onlyB.Add(new { p = eb.Original, s = eb.Size });

        long totalBytes = toCompare.Sum(x => x.A.Size), doneBytes = 0;
        foreach (var (ea, eb) in toCompare)
        {
            ct.ThrowIfCancellationRequested();
            await using Stream sa = fa.OpenRead(ea.Original);
            await using Stream sb = fb.OpenRead(eb.Original);
            long at = await FirstDifferenceAsync(sa, sb, n => { doneBytes += n; Bar(totalBytes == 0 ? 1 : (double)doneBytes / totalBytes, ea.Stripped); }, ct);
            if (at >= 0) differ.Add(new { p = ea.Stripped, sa = ea.Size, sb = eb.Size, why = "content", offset = at });
            else sameVerified++;
        }

        // Поля param.json: различающиеся и присутствующие только с одной стороны.
        var pa = FlattenParam(ga.RawParamJson);
        var pb = FlattenParam(gb.RawParamJson);
        var paramDiff = new List<object>();
        foreach (string key in pa.Keys.Union(pb.Keys).OrderBy(k => k, StringComparer.Ordinal))
        {
            pa.TryGetValue(key, out string? va);
            pb.TryGetValue(key, out string? vb);
            if (va != vb) paramDiff.Add(new { key, a = va, b = vb });
        }

        // Обязательные файлы в корне игры.
        string[] required = ["eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png"];
        object Required(Dictionary<string, Entry> idx) => required.Select(r => new { path = r, present = idx.ContainsKey(r) }).ToList();

        var report = new
        {
            deep,
            a = SideInfo(ga, fa.Files.Count, fa.Files.Sum(f => f.Size), ebootA, prefA),
            b = SideInfo(gb, fb.Files.Count, fb.Files.Sum(f => f.Size), ebootB, prefB),
            requiredA = Required(ia), requiredB = Required(ib),
            onlyA, onlyB, differ, caseDiff, paramDiff,
            sameVerified, sameBySizeOnly, comparedBytes = totalBytes
        };
        File.WriteAllText(outFile, JsonSerializer.Serialize(report), new UTF8Encoding(false));
        Bar(1, "done");
        Emit(new { type = "result", file = outFile });
        return 0;
    }

    // ---------------------------------------------------------------- обслуживание образов

    private static IProgress<FfpfscProgress> MakeProgress() => new Sync<FfpfscProgress>(p =>
        Bar(p.TotalBytes > 0 ? (double)p.BytesProcessed / p.TotalBytes : 0, p.Stage));

    private static string Positional(Options o, int index, string what) =>
        o.Positional.Count > index ? o.Positional[index] : throw new ArgumentException($"<{what}> is required");

    // Формат определяется по содержимому, а не по расширению: образ может называться как угодно.
    private static Ps5ImageFormat RequireFormat(string image, string what, params Ps5ImageFormat[] allowed)
    {
        if (!File.Exists(image)) throw new FileNotFoundException("Image not found: " + image);
        Ps5ImageFormat format = Ps5ImageConversionService.DetectSource(image);
        if (!allowed.Contains(format))
            throw new InvalidDataException($"{what} is not available for this image (detected format: {format}). Supported: {string.Join(", ", allowed)}.");
        return format;
    }

    private static async Task<int> VerifyImageAsync(Options o, CancellationToken ct)
    {
        string image = Positional(o, 0, "image");
        if (!File.Exists(image)) throw new FileNotFoundException("Image not found: " + image);
        if (Path.GetExtension(image).Equals(".ffpfsc", StringComparison.OrdinalIgnoreCase))
        {
            FfpfscVerificationResult f = await FfpfscImage.VerifyAsync(image, null, MakeProgress(), ct);
            Bar(1, "done");
            Console.WriteLine($"FFPFSC: structure valid = {f.StructureValid}, every PFSC block decodes = {f.EveryPfscBlockDecodes}");
            if (!string.IsNullOrEmpty(f.Error)) Console.WriteLine("Error: " + f.Error);
            return f.StructureValid && f.EveryPfscBlockDecodes ? 0 : 1;
        }
        Ps5ImageFormat format = RequireFormat(image, "Verify", Ps5ImageFormat.Exfat, Ps5ImageFormat.Ufs2);
        if (format == Ps5ImageFormat.Exfat)
        {
            ExfatVerificationResult r = await ExfatImage.VerifyAsync(image, MakeProgress(), ct);
            Bar(1, "done");
            Console.WriteLine($"exFAT OK: {r.FileCount} files, {r.DirectoryCount} directories, {r.LogicalFileBytes} bytes, manifest sha256 {r.ManifestSha256}");
            return 0;
        }
        Ufs2VerificationResult u = await Ufs2Operations.VerifyAsync(image, UfsProgress(), ct);
        Bar(1, "done");
        Console.WriteLine($"FFPKG (UFS2): {u.FileCount} files, {u.DirectoryCount} directories, {u.LogicalFileBytes} bytes, fsck errors {u.FsckErrors}, warnings {u.FsckWarnings}");
        return u.FsckErrors == 0 ? 0 : 1;
    }

    private static IProgress<Ufs2Progress> UfsProgress() => new Sync<Ufs2Progress>(p =>
        Bar(p.Total > 0 ? (double)p.Completed / p.Total : 0, p.Stage));

    private static async Task<int> RebuildAsync(Options o, CancellationToken ct)
    {
        string image = Positional(o, 0, "image");
        RequireFormat(image, "Rebuild", Ps5ImageFormat.Ufs2);
        Ufs2VerificationResult u = await Ufs2Operations.RebuildWithEditsAsync(image, static _ => { }, UfsProgress(), ct);
        Bar(1, "done");
        Console.WriteLine($"FFPKG rebuilt: {u.FileCount} files, fsck errors {u.FsckErrors}, warnings {u.FsckWarnings}");
        return u.FsckErrors == 0 ? 0 : 1;
    }

    private static async Task<int> RepairAsync(Options o, CancellationToken ct)
    {
        string image = Positional(o, 0, "image");
        RequireFormat(image, "Repair", Ps5ImageFormat.Exfat);
        ExfatRepairResult r = await ExfatImageMaintenance.RepairAsync(image, MakeProgress(), ct);
        Bar(1, "done");
        Console.WriteLine($"Repair finished. Boot region recovered: {r.BootRegionRecovered}");
        Console.WriteLine(JsonSerializer.Serialize(r.Verification));
        return 0;
    }

    private static async Task<int> RefreshAmprAsync(Options o, CancellationToken ct)
    {
        string image = Positional(o, 0, "image");
        RequireFormat(image, "AMPR refresh", Ps5ImageFormat.Exfat);
        ExfatAmprRefreshResult r = await ExfatAmprPatcher.RefreshAsync(image, MakeProgress(), ct);
        Bar(1, "done");
        Console.WriteLine($"AMPR index rebuilt: {r.RecordCount} records, {r.IndexBytes} bytes, {r.ClusterCount} cluster(s), sha256 {r.Sha256}");
        return 0;
    }

    private static async Task<int> EditAsync(Options o, CancellationToken ct)
    {
        string image = Positional(o, 0, "image");
        Ps5ImageFormat format = RequireFormat(image, "Editing", Ps5ImageFormat.Exfat, Ps5ImageFormat.Ufs2);

        // (вид правки, путь в образе, источник на диске)
        var ops = new List<(string Kind, string ImagePath, string? Source)>();
        foreach (string[] pair in o.Pairs("replace")) ops.Add(("replace", pair[0], pair[1]));
        foreach (string[] pair in o.Pairs("add")) ops.Add(("add", pair[0], pair[1]));
        foreach (string[] pair in o.Pairs("add-tree")) ops.Add(("add-tree", pair[0], pair[1]));
        foreach (string path in o.All("mkdir")) ops.Add(("mkdir", path, null));
        foreach (string path in o.All("delete")) ops.Add(("delete", path, null));
        if (ops.Count == 0) throw new ArgumentException("No edit operations given");

        if (format == Ps5ImageFormat.Exfat)
        {
            var list = ops.Select(x => x.Kind switch
            {
                "replace" => ExfatEditOperation.Replace(x.ImagePath, x.Source!),
                "add" => ExfatEditOperation.AddFile(x.ImagePath, x.Source!),
                "add-tree" => ExfatEditOperation.AddDirectoryTree(x.ImagePath, x.Source!),
                "mkdir" => ExfatEditOperation.AddDirectory(x.ImagePath),
                _ => ExfatEditOperation.Delete(x.ImagePath)
            }).ToList();
            ExfatEditResult r = await ExfatImageMaintenance.ApplyEditsAsync(image, list, MakeProgress(), ct);
            Bar(1, "done");
            Console.WriteLine($"Applied {r.OperationCount} operation(s). Image rebuilt: {r.Rebuilt}");
            Console.WriteLine(JsonSerializer.Serialize(r.Verification));
        }
        else
        {
            var list = ops.Select(x => x.Kind switch
            {
                "replace" => Ufs2EditOperation.Replace(x.ImagePath, x.Source!),
                "add" => Ufs2EditOperation.AddFile(x.ImagePath, x.Source!),
                "add-tree" => Ufs2EditOperation.AddDirectoryTree(x.ImagePath, x.Source!),
                "mkdir" => Ufs2EditOperation.AddDirectory(x.ImagePath),
                _ => Ufs2EditOperation.Delete(x.ImagePath)
            }).ToList();
            Ufs2EditResult r = await Ufs2Operations.ApplyEditsAsync(image, list, UfsProgress(), ct);
            Bar(1, "done");
            Console.WriteLine($"Applied {r.OperationCount} operation(s) to FFPKG. fsck errors {r.Verification.FsckErrors}, warnings {r.Verification.FsckWarnings}");
        }
        return 0;
    }

    private static async Task<int> ConvertAsync(Options o, CancellationToken ct)
    {
        string source = Positional(o, 0, "source");
        string output = Positional(o, 1, "output");
        Ps5ImageConversionTarget target = o.Require("to").ToLowerInvariant() switch
        {
            "exfat" => Ps5ImageConversionTarget.Exfat,
            "ffpkg" => Ps5ImageConversionTarget.Ffpkg,
            "ffpfsc" => Ps5ImageConversionTarget.Ffpfsc,
            _ => throw new ArgumentException("--to must be exfat, ffpkg or ffpfsc")
        };
        var progress = new Sync<Ps5ImageConversionProgress>(p =>
            Bar(p.Total > 0 ? (double)p.Completed / p.Total : 0, p.Stage));
        Ps5ImageConversionResult r = await Ps5ImageConversionService.ConvertAsync(source, output, target,
            o.Has("overwrite"), progress, ct);
        Bar(1, "done");
        Console.WriteLine($"Converted {r.SourceFormat} -> {r.Target}: {r.FileCount} file(s), {r.SourceBytes} -> {r.OutputBytes} bytes");
        Console.WriteLine("Output: " + r.OutputPath);
        return 0;
    }

    // ---------------------------------------------------------------- PNG

    // Ps5ImageData: PNG-байты возвращаются как есть, RGBA кодируем минимальным PNG-писателем (без внешних зависимостей).
    private static string SaveImage(Ps5ImageData? image, string path)
    {
        if (image is null || image.IsEmpty) return "";
        if (!image.IsRgba) File.WriteAllBytes(path, image.Bytes);
        else File.WriteAllBytes(path, PngWriter.EncodeRgba(image.Bytes, image.Width, image.Height));
        return Path.GetFileName(path);
    }
}

internal sealed class Options
{
    private readonly Dictionary<string, List<string[]>> _values = new(StringComparer.OrdinalIgnoreCase);
    public List<string> Positional { get; } = [];

    // Опции с двумя аргументами (пары «путь в образе» + «источник»).
    private static readonly HashSet<string> TwoArgs = new(StringComparer.OrdinalIgnoreCase) { "replace", "add", "add-tree" };
    private static readonly HashSet<string> Flags = new(StringComparer.OrdinalIgnoreCase) { "no-recursive", "overwrite", "deep" };

    public static Options Parse(IEnumerable<string> args)
    {
        var o = new Options();
        string[] a = args.ToArray();
        for (int i = 0; i < a.Length; i++)
        {
            if (!a[i].StartsWith("--", StringComparison.Ordinal)) { o.Positional.Add(a[i]); continue; }
            string key = a[i][2..];
            int count = Flags.Contains(key) ? 0 : TwoArgs.Contains(key) ? 2 : 1;
            if (count > 0 && i + count >= a.Length)
                throw new ArgumentException($"Option --{key} needs {count} value(s)");
            var vals = new string[count];
            for (int k = 0; k < count; k++) vals[k] = a[++i];
            if (!o._values.TryGetValue(key, out var list)) o._values[key] = list = [];
            list.Add(vals);
        }
        return o;
    }

    public bool Has(string key) => _values.ContainsKey(key);
    public string? Get(string key) => _values.TryGetValue(key, out var l) && l.Count > 0 && l[^1].Length > 0 ? l[^1][0] : null;
    public string Require(string key) => Get(key) ?? throw new ArgumentException($"--{key} is required");
    public List<string> All(string key) => _values.TryGetValue(key, out var l) ? l.Where(v => v.Length > 0).Select(v => v[0]).ToList() : [];
    public List<string[]> Pairs(string key) => _values.TryGetValue(key, out var l) ? l : [];
}

internal static class PngWriter
{
    public static byte[] EncodeRgba(byte[] rgba, int width, int height)
    {
        using var ms = new MemoryStream();
        ms.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);

        var ihdr = new byte[13];
        WriteBe(ihdr, 0, (uint)width);
        WriteBe(ihdr, 4, (uint)height);
        ihdr[8] = 8;      // 8 бит на канал
        ihdr[9] = 6;      // RGBA
        Chunk(ms, "IHDR", ihdr);

        int stride = width * 4;
        var raw = new byte[(stride + 1) * height];
        for (int y = 0; y < height; y++)
        {
            raw[y * (stride + 1)] = 0;                                   // фильтр «None»
            Buffer.BlockCopy(rgba, y * stride, raw, y * (stride + 1) + 1, stride);
        }
        using var z = new MemoryStream();
        using (var zs = new System.IO.Compression.ZLibStream(z, System.IO.Compression.CompressionLevel.Fastest, true))
            zs.Write(raw, 0, raw.Length);
        Chunk(ms, "IDAT", z.ToArray());
        Chunk(ms, "IEND", []);
        return ms.ToArray();
    }

    private static void WriteBe(byte[] b, int o, uint v)
    {
        b[o] = (byte)(v >> 24); b[o + 1] = (byte)(v >> 16); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v;
    }

    private static void Chunk(Stream s, string type, byte[] data)
    {
        var len = new byte[4];
        WriteBe(len, 0, (uint)data.Length);
        s.Write(len);
        byte[] t = System.Text.Encoding.ASCII.GetBytes(type);
        s.Write(t);
        s.Write(data);
        uint crc = Crc32(Crc32(0, t), data);
        var c = new byte[4];
        WriteBe(c, 0, crc);
        s.Write(c);
    }

    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        return table;
    }

    private static uint Crc32(uint crc, byte[] data)
    {
        uint c = crc ^ 0xFFFFFFFFu;
        foreach (byte b in data) c = Table[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }
}
