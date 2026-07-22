using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

internal static class Program
{
    private const int RpcECallRejected = unchecked((int)0x80010001);
    private const int RpcEServerCallRetryLater = unchecked((int)0x8001010A);

    [DllImport("ole32.dll")]
    private static extern int GetRunningObjectTable(
        int reserved,
        out IRunningObjectTable? runningObjectTable);

    [DllImport("ole32.dll")]
    private static extern int CreateBindCtx(
        int reserved,
        out IBindCtx? bindContext);

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("VisualStudioBridge command and project root are required.");
                return 1;
            }

            string command = args[0];
            bool opensScript = string.Equals(
                command,
                "open-script",
                StringComparison.OrdinalIgnoreCase);
            bool opensProject = string.Equals(
                command,
                "open-project",
                StringComparison.OrdinalIgnoreCase);
            if (!opensScript && !opensProject)
            {
                Console.Error.WriteLine($"Unsupported command: {command}");
                return 1;
            }

            string projectRoot = Path.GetFullPath(args[1]);
            if (!Directory.Exists(projectRoot) ||
                !File.Exists(Path.Combine(projectRoot, "CMakeLists.txt")))
            {
                Console.Error.WriteLine($"GameScript CMake project was not found: {projectRoot}");
                return 1;
            }

            string? sourcePath = opensScript && args.Length >= 3
                ? Path.GetFullPath(args[2])
                : null;
            if (opensScript &&
                (string.IsNullOrWhiteSpace(sourcePath) || !File.Exists(sourcePath)))
            {
                Console.Error.WriteLine($"Script source was not found: {sourcePath}");
                return 1;
            }

            if (opensProject)
            {
                StartCMakeProject(projectRoot);
                return 0;
            }

            OpenScript(projectRoot, sourcePath!);
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static void OpenScript(string projectRoot, string sourcePath)
    {
        dynamic? dte = FindVisualStudioInstance(projectRoot, null);
        if (dte is null)
        {
            int processId = StartCMakeProject(projectRoot);
            dte = WaitForVisualStudioInstance(projectRoot, processId);
        }

        RetryComCall(() => dte.MainWindow.Visible = true);
        RetryComCall(() => dte.ItemOperations.OpenFile(sourcePath));
        RetryComCall(() => dte.UserControl = true);
        RetryComCall(() => dte.MainWindow.Activate());
    }

    private static int StartCMakeProject(string projectRoot)
    {
        string? devenvPath = FindDevenvPath();
        if (string.IsNullOrWhiteSpace(devenvPath))
        {
            throw new InvalidOperationException("devenv.exe was not found.");
        }

        ProcessStartInfo startInfo = new()
        {
            FileName = devenvPath,
            WorkingDirectory = projectRoot,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add(projectRoot);

        using Process? process = Process.Start(startInfo);
        if (process is null)
        {
            throw new InvalidOperationException("Visual Studio process could not be started.");
        }

        return process.Id;
    }

    private static dynamic WaitForVisualStudioInstance(
        string projectRoot,
        int processId)
    {
        const int retryCount = 240;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            dynamic? dte = FindVisualStudioInstance(projectRoot, processId);
            if (dte is not null)
            {
                return dte;
            }

            Thread.Sleep(250);
        }

        throw new TimeoutException("GameScript CMake project did not finish opening.");
    }

    private static dynamic? FindVisualStudioInstance(
        string projectRoot,
        int? processId)
    {
        IRunningObjectTable? runningObjectTable = null;
        IEnumMoniker? enumMoniker = null;
        try
        {
            if (GetRunningObjectTable(0, out runningObjectTable) != 0 ||
                runningObjectTable is null)
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
                object? runningObject = null;
                bool keepsRunningObject = false;
                try
                {
                    CreateBindCtx(0, out bindContext);
                    if (bindContext is null)
                    {
                        continue;
                    }

                    monikers[0].GetDisplayName(bindContext, null, out string displayName);
                    if (displayName.IndexOf(
                            "VisualStudio.DTE.",
                            StringComparison.OrdinalIgnoreCase) < 0)
                    {
                        continue;
                    }
                    if (processId.HasValue &&
                        !displayName.EndsWith(
                            $":{processId.Value}",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    runningObjectTable.GetObject(monikers[0], out runningObject);
                    if (runningObject is null)
                    {
                        continue;
                    }

                    dynamic dte = runningObject;
                    if (processId.HasValue || IsProjectInstance(dte, projectRoot))
                    {
                        keepsRunningObject = true;
                        return dte;
                    }
                }
                catch
                {
                }
                finally
                {
                    if (runningObject is not null && !keepsRunningObject)
                    {
                        Marshal.ReleaseComObject(runningObject);
                    }
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

        return null;
    }

    private static bool IsProjectInstance(dynamic dte, string projectRoot)
    {
        try
        {
            string caption = dte.MainWindow?.Caption as string ?? string.Empty;
            string projectName = Path.GetFileName(
                projectRoot.TrimEnd(
                    Path.DirectorySeparatorChar,
                    Path.AltDirectorySeparatorChar));
            return !string.IsNullOrWhiteSpace(projectName) &&
                   caption.IndexOf(projectName, StringComparison.OrdinalIgnoreCase) >= 0;
        }
        catch
        {
            return false;
        }
    }

    private static void RetryComCall(Action action)
    {
        const int retryCount = 240;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            try
            {
                action();
                return;
            }
            catch (COMException exception) when (
                exception.HResult == RpcECallRejected ||
                exception.HResult == RpcEServerCallRetryLater)
            {
                // CMake workspace の初期化中だけ発生する拒否は同じ instance へ再送する
                Thread.Sleep(250);
            }
        }

        throw new TimeoutException("Visual Studio remained busy while handling the request.");
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

    private static IEnumerable<string> EnumerateVisualStudioInstallations()
    {
        foreach (string visualStudioRoot in EnumerateVisualStudioRoots())
        {
            if (!Directory.Exists(visualStudioRoot))
            {
                continue;
            }

            foreach (string versionDirectory in Directory.EnumerateDirectories(visualStudioRoot)
                         .OrderByDescending(path => path, StringComparer.OrdinalIgnoreCase))
            {
                foreach (string editionDirectory in Directory.EnumerateDirectories(versionDirectory)
                             .OrderBy(path => path, StringComparer.OrdinalIgnoreCase))
                {
                    yield return editionDirectory;
                }
            }
        }
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
}
