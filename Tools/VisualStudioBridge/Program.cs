using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;

namespace Cue.VisualStudioBridge;

internal enum BuildStage
{
    General,
    Configure,
    Build,
    Reload,
    Attach
}

internal enum BuildMessageSeverity
{
    Info,
    Warning,
    Error
}

internal sealed class ToolBuildStageResult
{
    public string Stage { get; set; } = BuildStage.General.ToString();
    public string Command { get; set; } = string.Empty;
    public string Output { get; set; } = string.Empty;
    public string LogPath { get; set; } = string.Empty;
    public uint ExitCode { get; set; }
    public bool Succeeded { get; set; }
}

internal sealed class ToolBuildMessage
{
    public string Severity { get; set; } = BuildMessageSeverity.Info.ToString();
    public string Stage { get; set; } = BuildStage.General.ToString();
    public string Text { get; set; } = string.Empty;
}

internal sealed class ToolBuildArtifact
{
    public string Name { get; set; } = string.Empty;
    public string Path { get; set; } = string.Empty;
}

internal sealed class ToolBuildResult
{
    public bool Succeeded { get; set; }
    public bool DidConfigure { get; set; }
    public uint ExitCode { get; set; }
    public string Summary { get; set; } = string.Empty;
    public string ConfigureLogPath { get; set; } = string.Empty;
    public string BuildLogPath { get; set; } = string.Empty;
    public List<ToolBuildStageResult> StageResults { get; } = new();
    public List<ToolBuildMessage> Messages { get; } = new();
    public List<ToolBuildArtifact> Artifacts { get; } = new();
}

internal sealed class BuildScriptOptions
{
    public string ScriptRoot { get; set; } = string.Empty;
    public string ConfigurePreset { get; set; } = string.Empty;
    public string Configuration { get; set; } = string.Empty;
    public string Target { get; set; } = string.Empty;
    public string ResultPath { get; set; } = string.Empty;
    public string ConfigureLogPath { get; set; } = string.Empty;
    public string BuildLogPath { get; set; } = string.Empty;
}

internal sealed class SolutionOptions
{
    public string ScriptRoot { get; set; } = string.Empty;
    public string ConfigurePreset { get; set; } = string.Empty;
}

internal sealed class AttachDebuggerOptions
{
    public string ScriptRoot { get; set; } = string.Empty;
    public string ConfigurePreset { get; set; } = string.Empty;
    public uint ProcessId { get; set; }
}

[SupportedOSPlatform("windows")]
internal static class Program
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never
    };

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.Error.WriteLine("Command is required.");
            return 1;
        }

        string command = args[0];
        string[] commandArgs = args.Skip(1).ToArray();
        try
        {
            if (string.Equals(command, "build-script", StringComparison.OrdinalIgnoreCase))
            {
                BuildScriptOptions? options = ParseBuildScriptOptions(commandArgs);
                if (options is null)
                {
                    return 1;
                }

                ToolBuildResult result = ExecuteBuildScript(options);
                WriteBuildResult(options.ResultPath, result);
                return result.Succeeded ? 0 : 1;
            }

            if (string.Equals(command, "open-solution", StringComparison.OrdinalIgnoreCase))
            {
                SolutionOptions? options = ParseSolutionOptions(commandArgs);
                if (options is null)
                {
                    return 1;
                }

                OpenSolution(options);
                return 0;
            }

            if (string.Equals(command, "attach-debugger", StringComparison.OrdinalIgnoreCase))
            {
                AttachDebuggerOptions? options = ParseAttachDebuggerOptions(commandArgs);
                if (options is null)
                {
                    return 1;
                }

                AttachDebugger(options);
                return 0;
            }

            Console.Error.WriteLine($"Unsupported command: {command}");
            return 1;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    private static BuildScriptOptions? ParseBuildScriptOptions(string[] args)
    {
        Dictionary<string, string>? values = ParseArgumentMap(args);
        if (values is null)
        {
            return null;
        }

        string[] requiredKeys =
        {
            "--script-root",
            "--configure-preset",
            "--configuration",
            "--target",
            "--result-path",
            "--configure-log-path",
            "--build-log-path"
        };

        foreach (string requiredKey in requiredKeys)
        {
            if (!values.ContainsKey(requiredKey))
            {
                Console.Error.WriteLine($"Missing argument: {requiredKey}");
                return null;
            }
        }

        return new BuildScriptOptions
        {
            ScriptRoot = values["--script-root"],
            ConfigurePreset = values["--configure-preset"],
            Configuration = values["--configuration"],
            Target = values["--target"],
            ResultPath = values["--result-path"],
            ConfigureLogPath = values["--configure-log-path"],
            BuildLogPath = values["--build-log-path"]
        };
    }

    private static SolutionOptions? ParseSolutionOptions(string[] args)
    {
        Dictionary<string, string>? values = ParseArgumentMap(args);
        if (values is null)
        {
            return null;
        }

        string[] requiredKeys =
        {
            "--script-root",
            "--configure-preset"
        };

        foreach (string requiredKey in requiredKeys)
        {
            if (!values.ContainsKey(requiredKey))
            {
                Console.Error.WriteLine($"Missing argument: {requiredKey}");
                return null;
            }
        }

        return new SolutionOptions
        {
            ScriptRoot = values["--script-root"],
            ConfigurePreset = values["--configure-preset"]
        };
    }

    private static AttachDebuggerOptions? ParseAttachDebuggerOptions(string[] args)
    {
        Dictionary<string, string>? values = ParseArgumentMap(args);
        if (values is null)
        {
            return null;
        }

        string[] requiredKeys =
        {
            "--script-root",
            "--configure-preset",
            "--process-id"
        };

        foreach (string requiredKey in requiredKeys)
        {
            if (!values.ContainsKey(requiredKey))
            {
                Console.Error.WriteLine($"Missing argument: {requiredKey}");
                return null;
            }
        }

        if (!uint.TryParse(
                values["--process-id"],
                NumberStyles.Integer,
                CultureInfo.InvariantCulture,
                out uint processId))
        {
            Console.Error.WriteLine("Invalid process id.");
            return null;
        }

        return new AttachDebuggerOptions
        {
            ScriptRoot = values["--script-root"],
            ConfigurePreset = values["--configure-preset"],
            ProcessId = processId
        };
    }

    private static Dictionary<string, string>? ParseArgumentMap(string[] args)
    {
        Dictionary<string, string> values = new(StringComparer.OrdinalIgnoreCase);
        for (int index = 0; index < args.Length; index += 2)
        {
            if (index + 1 >= args.Length ||
                !args[index].StartsWith("--", StringComparison.Ordinal))
            {
                Console.Error.WriteLine("Invalid arguments.");
                return null;
            }

            values[args[index]] = args[index + 1];
        }

        return values;
    }

    private static ToolBuildResult ExecuteBuildScript(BuildScriptOptions options)
    {
        ToolBuildResult result = new()
        {
            ConfigureLogPath = options.ConfigureLogPath,
            BuildLogPath = options.BuildLogPath
        };

        try
        {
            string configureDirectory = Path.Combine(
                options.ScriptRoot,
                "out",
                "build",
                options.ConfigurePreset);

            if (!Directory.Exists(options.ScriptRoot))
            {
                return Fail(result, 1, BuildStage.General,
                    $"Script root does not exist: {options.ScriptRoot}");
            }

            string? solutionPath = FindSolutionFile(configureDirectory);
            if (solutionPath is null)
            {
                ToolBuildStageResult configureStage = ExecuteProcess(
                    "cmake",
                    $"--preset {options.ConfigurePreset} --fresh",
                    options.ScriptRoot,
                    options.ConfigureLogPath,
                    BuildStage.Configure);
                result.StageResults.Add(configureStage);
                result.DidConfigure = true;
                if (!configureStage.Succeeded)
                {
                    result.ExitCode = configureStage.ExitCode;
                    return Fail(result, configureStage.ExitCode, BuildStage.Configure,
                        "VisualStudioBridge configure failed.");
                }

                solutionPath = FindSolutionFile(configureDirectory);
                if (solutionPath is null)
                {
                    return Fail(result, 1, BuildStage.Configure,
                        "Configured solution file was not found.");
                }
            }

            string? msBuildPath = FindMsBuildPath();
            if (string.IsNullOrWhiteSpace(msBuildPath))
            {
                return Fail(result, 1, BuildStage.Build,
                    "MSBuild.exe was not found.");
            }

            string quotedSolutionPath = Quote(solutionPath);
            string buildArguments =
                $"{quotedSolutionPath} /t:{options.Target} /p:Configuration={options.Configuration} /nologo";
            ToolBuildStageResult buildStage = ExecuteProcess(
                msBuildPath,
                buildArguments,
                options.ScriptRoot,
                options.BuildLogPath,
                BuildStage.Build);
            result.StageResults.Add(buildStage);
            result.ExitCode = buildStage.ExitCode;
            if (!buildStage.Succeeded)
            {
                return Fail(result, buildStage.ExitCode, BuildStage.Build,
                    "VisualStudioBridge build failed.");
            }

            string artifactPath = Path.Combine(
                configureDirectory,
                options.Target,
                options.Configuration,
                $"{options.Target}.dll");
            if (File.Exists(artifactPath))
            {
                result.Artifacts.Add(new ToolBuildArtifact
                {
                    Name = options.Target,
                    Path = artifactPath
                });
            }

            result.Succeeded = true;
            result.Summary = "VisualStudioBridge build succeeded.";
            result.Messages.Add(new ToolBuildMessage
            {
                Severity = BuildMessageSeverity.Info.ToString(),
                Stage = BuildStage.Build.ToString(),
                Text = result.Summary
            });
            return result;
        }
        catch (Exception ex)
        {
            return Fail(result, 1, BuildStage.General, ex.Message);
        }
    }

    private static void OpenSolution(SolutionOptions options)
    {
        OpenFolderInVisualStudio(options.ScriptRoot);
    }

    private static void AttachDebugger(AttachDebuggerOptions options)
    {
        string solutionPath = EnsureSolutionPath(
            options.ScriptRoot, options.ConfigurePreset);
        dynamic dte = FindOrOpenVisualStudioInstance(
            options.ScriptRoot,
            solutionPath);
        dte.MainWindow.Visible = true;
        dte.UserControl = true;

        const int retryCount = 20;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            foreach (dynamic process in dte.Debugger.LocalProcesses)
            {
                int processId = Convert.ToInt32(
                    process.ProcessID, CultureInfo.InvariantCulture);
                if (processId != options.ProcessId)
                {
                    continue;
                }

                process.Attach();
                return;
            }

            Thread.Sleep(500);
        }

        throw new InvalidOperationException(
            $"Process {options.ProcessId} was not found in Visual Studio debugger.");
    }

    private static ToolBuildResult Fail(
        ToolBuildResult result,
        uint exitCode,
        BuildStage stage,
        string message)
    {
        result.Succeeded = false;
        result.ExitCode = exitCode;
        result.Summary = message;
        result.Messages.Add(new ToolBuildMessage
        {
            Severity = BuildMessageSeverity.Error.ToString(),
            Stage = stage.ToString(),
            Text = message
        });
        return result;
    }

    private static string EnsureSolutionPath(
        string scriptRoot,
        string configurePreset)
    {
        if (!Directory.Exists(scriptRoot))
        {
            throw new DirectoryNotFoundException(
                $"Script root does not exist: {scriptRoot}");
        }

        string configureDirectory = Path.Combine(
            scriptRoot,
            "out",
            "build",
            configurePreset);
        string? solutionPath = FindSolutionFile(configureDirectory);
        if (!string.IsNullOrWhiteSpace(solutionPath))
        {
            return solutionPath;
        }

        string configureLogPath = Path.GetTempFileName();
        try
        {
            ToolBuildStageResult configureStage = ExecuteProcess(
                "cmake",
                $"--preset {configurePreset} --fresh",
                scriptRoot,
                configureLogPath,
                BuildStage.Configure);
            if (!configureStage.Succeeded)
            {
                throw new InvalidOperationException(
                    "Failed to configure CMake preset for Visual Studio solution.");
            }
        }
        finally
        {
            TryDeleteFile(configureLogPath);
        }

        solutionPath = FindSolutionFile(configureDirectory);
        if (string.IsNullOrWhiteSpace(solutionPath))
        {
            throw new FileNotFoundException(
                "Visual Studio solution file was not found after configure.",
                configureDirectory);
        }

        return solutionPath;
    }

    private static dynamic FindOrOpenVisualStudioInstance(
        string scriptRoot,
        string solutionPath)
    {
        dynamic? existingDte = FindRunningVisualStudioDte(
            dte => IsMatchingVisualStudioInstance(dte, scriptRoot, solutionPath));
        if (existingDte is not null)
        {
            WaitForVisualStudioReady(existingDte);
            return existingDte;
        }

        return OpenSolutionInVisualStudio(solutionPath);
    }

    private static dynamic OpenSolutionInVisualStudio(string solutionPath)
    {
        dynamic dte = CreateVisualStudioDte();
        dte.MainWindow.Visible = true;
        dte.UserControl = true;

        string currentSolutionPath = string.Empty;
        try
        {
            currentSolutionPath = dte.Solution.FullName as string ?? string.Empty;
        }
        catch
        {
            currentSolutionPath = string.Empty;
        }

        if (!string.Equals(
                currentSolutionPath,
                solutionPath,
                StringComparison.OrdinalIgnoreCase))
        {
            dte.Solution.Open(solutionPath);
        }

        WaitForVisualStudioReady(dte);
        return dte;
    }

    private static dynamic? FindRunningVisualStudioDte(
        Func<dynamic, bool> predicate)
    {
        IRunningObjectTable? runningObjectTable = null;
        IEnumMoniker? enumMoniker = null;

        try
        {
            int hresult = GetRunningObjectTable(0, out runningObjectTable);
            if (hresult != 0 || runningObjectTable is null)
            {
                return null;
            }

            runningObjectTable.EnumRunning(out enumMoniker);
            if (enumMoniker is null)
            {
                return null;
            }

            IMoniker[] monikers = new IMoniker[1];
            while (enumMoniker.Next(1, monikers, IntPtr.Zero) == 0)
            {
                IBindCtx? bindContext = null;
                string displayName = string.Empty;

                try
                {
                    CreateBindCtx(0, out bindContext);
                    if (bindContext is null)
                    {
                        continue;
                    }

                    monikers[0].GetDisplayName(bindContext, null, out displayName);
                    if (string.IsNullOrWhiteSpace(displayName) ||
                        displayName.IndexOf("VisualStudio.DTE.",
                            StringComparison.OrdinalIgnoreCase) < 0)
                    {
                        continue;
                    }

                    runningObjectTable.GetObject(monikers[0], out object? runningObject);
                    if (runningObject is null)
                    {
                        continue;
                    }

                    dynamic dte = runningObject;
                    if (predicate(dte))
                    {
                        return dte;
                    }
                }
                catch
                {
                }
                finally
                {
                    if (bindContext is not null)
                    {
                        Marshal.ReleaseComObject(bindContext);
                    }

                    if (monikers[0] is not null)
                    {
                        Marshal.ReleaseComObject(monikers[0]);
                        monikers[0] = null!;
                    }
                }
            }

            return null;
        }
        finally
        {
            if (enumMoniker is not null)
            {
                Marshal.ReleaseComObject(enumMoniker);
            }

            if (runningObjectTable is not null)
            {
                Marshal.ReleaseComObject(runningObjectTable);
            }
        }
    }

    private static bool IsMatchingVisualStudioInstance(
        dynamic dte,
        string scriptRoot,
        string solutionPath)
    {
        try
        {
            string currentSolutionPath =
                dte.Solution?.FullName as string ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(currentSolutionPath) &&
                string.Equals(
                    currentSolutionPath,
                    solutionPath,
                    StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        catch
        {
        }

        try
        {
            string caption = dte.MainWindow?.Caption as string ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(caption))
            {
                string normalizedScriptRoot = Path.GetFullPath(scriptRoot)
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                string projectName = Path.GetFileName(normalizedScriptRoot);
                if ((!string.IsNullOrWhiteSpace(projectName) &&
                        caption.IndexOf(projectName, StringComparison.OrdinalIgnoreCase) >= 0) ||
                    caption.IndexOf(normalizedScriptRoot, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return true;
                }
            }
        }
        catch
        {
        }

        return false;
    }

    private static void OpenFolderInVisualStudio(string folderPath)
    {
        if (!Directory.Exists(folderPath))
        {
            throw new DirectoryNotFoundException(
                $"Script root does not exist: {folderPath}");
        }

        string? devenvPath = FindDevenvPath();
        if (string.IsNullOrWhiteSpace(devenvPath))
        {
            throw new InvalidOperationException(
                "devenv.exe was not found.");
        }

        ProcessStartInfo startInfo = new()
        {
            FileName = devenvPath,
            Arguments = Quote(folderPath),
            WorkingDirectory = folderPath,
            UseShellExecute = false
        };

        using Process? process = Process.Start(startInfo);
        if (process is null)
        {
            throw new InvalidOperationException(
                "Visual Studio process could not be started.");
        }
    }

    private static dynamic CreateVisualStudioDte()
    {
        foreach (string progId in EnumerateDteProgIds())
        {
            Type? dteType = Type.GetTypeFromProgID(progId, throwOnError: false);
            if (dteType is null)
            {
                continue;
            }

            object? dte = Activator.CreateInstance(dteType);
            if (dte is not null)
            {
                return dte;
            }
        }

        throw new InvalidOperationException(
            "Visual Studio DTE could not be created.");
    }

    private static IEnumerable<string> EnumerateDteProgIds()
    {
        SortedSet<int> majorVersions = new(Comparer<int>.Create(
            static (left, right) => right.CompareTo(left)));

        foreach (string visualStudioRoot in EnumerateVisualStudioRoots())
        {
            if (!Directory.Exists(visualStudioRoot))
            {
                continue;
            }

            foreach (string versionDirectory in Directory.EnumerateDirectories(visualStudioRoot))
            {
                string versionName = Path.GetFileName(versionDirectory);
                if (int.TryParse(
                        versionName,
                        NumberStyles.Integer,
                        CultureInfo.InvariantCulture,
                        out int majorVersion))
                {
                    majorVersions.Add(majorVersion);
                }
            }
        }

        foreach (int majorVersion in majorVersions)
        {
            yield return $"VisualStudio.DTE.{majorVersion}.0";
        }
    }

    private static string? FindDevenvPath()
    {
        foreach (string installationDirectory in EnumerateVisualStudioInstallations())
        {
            string candidate = Path.Combine(
                installationDirectory,
                "Common7",
                "IDE",
                "devenv.exe");
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static void WaitForVisualStudioReady(dynamic dte)
    {
        const int retryCount = 40;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            try
            {
                _ = dte.Debugger.LocalProcesses;
                return;
            }
            catch
            {
                Thread.Sleep(250);
            }
        }
    }

    private static void WriteBuildResult(string resultPath, ToolBuildResult result)
    {
        string? resultDirectory = Path.GetDirectoryName(resultPath);
        if (!string.IsNullOrEmpty(resultDirectory))
        {
            Directory.CreateDirectory(resultDirectory);
        }

        string json = JsonSerializer.Serialize(result, s_jsonOptions);
        File.WriteAllText(resultPath, json, new UTF8Encoding(false));
    }

    private static ToolBuildStageResult ExecuteProcess(
        string fileName,
        string arguments,
        string workingDirectory,
        string logPath,
        BuildStage stage)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);

        ProcessStartInfo startInfo = new()
        {
            FileName = fileName,
            Arguments = arguments,
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        using Process process = new() { StartInfo = startInfo };
        StringBuilder outputBuilder = new();
        process.OutputDataReceived += (_, eventArgs) =>
        {
            if (eventArgs.Data is not null)
            {
                outputBuilder.AppendLine(eventArgs.Data);
            }
        };
        process.ErrorDataReceived += (_, eventArgs) =>
        {
            if (eventArgs.Data is not null)
            {
                outputBuilder.AppendLine(eventArgs.Data);
            }
        };

        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        process.WaitForExit();

        string output = outputBuilder.ToString();
        File.WriteAllText(logPath, output, new UTF8Encoding(false));

        return new ToolBuildStageResult
        {
            Stage = stage.ToString(),
            Command = $"{fileName} {arguments}",
            Output = output,
            LogPath = logPath,
            ExitCode = unchecked((uint)process.ExitCode),
            Succeeded = process.ExitCode == 0
        };
    }

    private static string? FindSolutionFile(string configureDirectory)
    {
        if (!Directory.Exists(configureDirectory))
        {
            return null;
        }

        string? slnxPath = Directory.EnumerateFiles(configureDirectory, "*.slnx")
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
        if (!string.IsNullOrEmpty(slnxPath))
        {
            return slnxPath;
        }

        return Directory.EnumerateFiles(configureDirectory, "*.sln")
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
    }

    private static string? FindMsBuildPath()
    {
        foreach (string vsWherePath in EnumerateVsWherePaths())
        {
            if (!File.Exists(vsWherePath))
            {
                continue;
            }

            ToolBuildStageResult vsWhereResult = ExecuteProcess(
                vsWherePath,
                "-latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe",
                Environment.CurrentDirectory,
                Path.GetTempFileName(),
                BuildStage.General);
            string candidate = vsWhereResult.Output
                .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault() ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(candidate) && File.Exists(candidate))
            {
                return candidate;
            }
        }

        string? installedMsBuild = FindInstalledMsBuildPath();
        if (!string.IsNullOrWhiteSpace(installedMsBuild))
        {
            return installedMsBuild;
        }

        ToolBuildStageResult whereResult = ExecuteProcess(
            "where",
            "msbuild.exe",
            Environment.CurrentDirectory,
            Path.GetTempFileName(),
            BuildStage.General);
        string firstLine = whereResult.Output
            .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .FirstOrDefault() ?? string.Empty;
        return File.Exists(firstLine) ? firstLine : null;
    }

    private static IEnumerable<string> EnumerateVsWherePaths()
    {
        string programFiles =
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        string programFilesX86 =
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);

        yield return Path.Combine(
            programFilesX86,
            "Microsoft Visual Studio",
            "Installer",
            "vswhere.exe");
        yield return Path.Combine(
            programFiles,
            "Microsoft Visual Studio",
            "Installer",
            "vswhere.exe");
    }

    private static string? FindInstalledMsBuildPath()
    {
        List<string> candidates = new();

        foreach (string visualStudioRoot in EnumerateVisualStudioRoots())
        {
            if (!Directory.Exists(visualStudioRoot))
            {
                continue;
            }

            foreach (string versionDirectory in Directory.EnumerateDirectories(visualStudioRoot))
            {
                foreach (string editionDirectory in Directory.EnumerateDirectories(versionDirectory))
                {
                    string amd64Path = Path.Combine(
                        editionDirectory,
                        "MSBuild",
                        "Current",
                        "Bin",
                        "amd64",
                        "MSBuild.exe");
                    if (File.Exists(amd64Path))
                    {
                        candidates.Add(amd64Path);
                    }

                    string x86Path = Path.Combine(
                        editionDirectory,
                        "MSBuild",
                        "Current",
                        "Bin",
                        "MSBuild.exe");
                    if (File.Exists(x86Path))
                    {
                        candidates.Add(x86Path);
                    }
                }
            }
        }

        return candidates
            .OrderByDescending(path => path, StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
    }

    private static IEnumerable<string> EnumerateVisualStudioRoots()
    {
        string programFiles =
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        string programFilesX86 =
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);

        yield return Path.Combine(programFiles, "Microsoft Visual Studio");
        yield return Path.Combine(programFilesX86, "Microsoft Visual Studio");
    }

    private static IEnumerable<string> EnumerateVisualStudioInstallations()
    {
        foreach (string visualStudioRoot in EnumerateVisualStudioRoots())
        {
            if (!Directory.Exists(visualStudioRoot))
            {
                continue;
            }

            foreach (string versionDirectory in Directory.EnumerateDirectories(visualStudioRoot))
            {
                foreach (string editionDirectory in Directory.EnumerateDirectories(versionDirectory))
                {
                    yield return editionDirectory;
                }
            }
        }
    }

    private static string Quote(string value)
    {
        return "\"" + value + "\"";
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
        }
    }

    [DllImport("ole32.dll")]
    private static extern int CreateBindCtx(
        uint reserved,
        out IBindCtx bindContext);

    [DllImport("ole32.dll")]
    private static extern int GetRunningObjectTable(
        uint reserved,
        out IRunningObjectTable runningObjectTable);
}
