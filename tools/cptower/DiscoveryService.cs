using System.Diagnostics;
using System.Net;

namespace CpTower;

/// <summary>
/// Periodically reconciles the live host set: enumerate loopback listeners owned by the copilot
/// process, probe newly seen ports for AHP, and drop hosts whose listener has gone away. Enriches
/// labels from the `ahp-host-{port}.log` files when the probe did not report a working directory.
/// </summary>
public sealed class DiscoveryService(HostRegistry registry, ILogger<DiscoveryService> log) : BackgroundService
{
    private static readonly TimeSpan Interval = TimeSpan.FromSeconds(3);

    private static readonly string LogDir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".copilot", "logs");

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(Interval);
        do
        {
            try
            {
                await ReconcileAsync(stoppingToken);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                log.LogWarning(ex, "discovery tick failed");
            }
        }
        while (await timer.WaitForNextTickAsync(stoppingToken));
    }

    private async Task ReconcileAsync(CancellationToken token)
    {
        var copilotPids = Process.GetProcessesByName("copilot").Select(p => p.Id).ToHashSet();
        var allListeners = TcpTable.Listeners();
        var candidates = allListeners
            .Where(l => IsLoopback(l.Address) && copilotPids.Contains(l.OwningPid))
            .Select(l => l.Port)
            .ToHashSet();

        foreach (var host in registry.Snapshot())
        {
            if (!candidates.Contains(host.Port))
            {
                log.LogInformation("host gone: port {Port} ({Label})", host.Port, host.Label);
                registry.Remove(host.Port);
            }
        }

        foreach (var port in candidates)
        {
            if (registry.Contains(port))
            {
                continue;
            }

            var probe = await AhpProbe.ProbeAsync(port, token);
            if (!probe.Ok)
            {
                continue;
            }

            var label = probe.Label ?? LabelFromLog(port) ?? $"Copilot ({port})";
            registry.Add(new HostInfo(port, label, probe.Protocol ?? "unknown", probe.Sessions));
            log.LogInformation("host up: port {Port} proto {Proto} sessions {Sessions} ({Label})",
                port, probe.Protocol, probe.Sessions, label);
        }
    }

    private static bool IsLoopback(IPAddress addr) => IPAddress.IsLoopback(addr);

    private static string? LabelFromLog(int port)
    {
        try
        {
            var path = Path.Combine(LogDir, $"ahp-host-{port}.log");
            if (!File.Exists(path))
            {
                return null;
            }

            var first = File.ReadLines(path).FirstOrDefault();
            const string marker = " for ";
            var idx = first?.LastIndexOf(marker, StringComparison.Ordinal) ?? -1;
            return idx >= 0 ? first![(idx + marker.Length)..].Trim() : null;
        }
        catch
        {
            return null;
        }
    }
}
