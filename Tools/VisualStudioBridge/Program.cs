using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Cue.VisualStudioBridge;

internal enum BuildStage
{
    General,
    Configure,
    Build,
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

internal static class Program
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never
    };

    private static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.Error.WriteLine("Command is required.");
            return 1;
        }

        string command = args[0];
        string[] commandArgs = args.Skip(1).ToArray();
        if (!string.Equals(command, "build-script", StringComparison.OrdinalIgnoreCase))
        {
            Console.Error.WriteLine($"Unsupported command: {command}");
            return 1;
        }

        BuildScriptOptions? options = ParseBuildScriptOptions(commandArgs);
        if (options is null)
        {
            return 1;
        }

        ToolBuildResult result = ExecuteBuildScript(options);
        try
        {
            WriteBuildResult(options.ResultPath, result);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }

        return result.Succeeded ? 0 : 1;
    }

    private static BuildScriptOptions? ParseBuildScriptOptions(string[] args)
    {
        Dictionary<string, string> values = new(StringComparer.OrdinalIgnoreCase);
        for (int index = 0; index < args.Length; index += 2)
        {
            if (index + 1 >= args.Length || !args[index].StartsWith("--", StringComparison.Ordinal))
            {
                Console.Error.WriteLine("Invalid arguments.");
                return null;
            }

            values[args[index]] = args[index + 1];
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
                string configureCommand = $"cmake --preset {options.ConfigurePreset} --fresh";
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

    private static string Quote(string value)
    {
        return "\"" + value + "\"";
    }
}
