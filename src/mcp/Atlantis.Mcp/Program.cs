// Plan 0070 M1 (Spec 0070 R1-R2; ADR-0138 D1): the MCP host's skeleton.
// One process, one Runtime session: the session file is found exactly as
// the CLI finds it (--session, else ATLANTIS_SESSION, else the default
// path), the connection is the C# SDK's, and the MCP server speaks stdio
// through the official SDK (pinned, J1) used directly -- McpServer.Create
// over StdioServerTransport, with the tools wired by the host's own
// registry (no Hosting/DI packages; the allow-listed set stays one
// package). The host exits when its stdin closes (the agent host went
// away) or the Runtime session dies; it opens no listening socket of its
// own. This milestone's one tool is control.status -- the exact tier's
// first entry and the skeleton's proof; the tier and the composed tools
// are M2/M3.
using System;
using System.Collections.Immutable;
using System.ComponentModel;
using System.Threading.Tasks;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace Atlantis.Mcp;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        string? path = null;
        for (int i = 0; i + 1 < args.Length; ++i)
        {
            if (args[i] == "--session") path = args[i + 1];
        }
        SessionInfo info;
        try
        {
            info = SessionFile.Read(SessionFile.Resolve(path));
        }
        catch (SessionFileException e)
        {
            Console.Error.WriteLine("atlantis-mcp: no Runtime session: " + e.Message);
            return 3;
        }

        RemoteSession session;
        try
        {
            session = RemoteSession.Connect(info);
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("atlantis-mcp: connect failed: " + e.Message);
            return 3;
        }
        using RemoteSession connection = session;

        // The session watcher: a Runtime that goes away ends the host (Spec R1). The wire's calls
        // are answered at frame boundaries, so a status unanswered after seconds is a dead session.
        _ = Task.Run(async () =>
        {
            while (true)
            {
                await Task.Delay(TimeSpan.FromSeconds(5));
                try
                {
                    _ = connection.Control.Status();
                }
                catch (Exception)
                {
                    Environment.Exit(0);
                }
            }
        });

        using StderrLoggerFactory logs = new();

        var registry = new ToolRegistry(
        [
            McpServerTool.Create(
                ([Description(
                    "The Runtime's status: paused, the frame count, the scene and its generation. Editing happens "
                    + "on the authoring document; authoring.apply makes it the running scene.")]
                (IServiceProvider services) => services.GetRequiredService<RemoteSession>().Control.Status()),
                new McpServerToolCreateOptions { Name = "control.status", ReadOnly = true, Idempotent = true }),
        ]);
        var options = new McpServerOptions
        {
            ServerInfo = new Implementation { Name = "atlantis-mcp", Version = "0.1.0" },
            Capabilities = new ServerCapabilities { Tools = new ToolsCapability() },
        };
        registry.Wire(options);
        await using McpServer server = McpServer.Create(new StdioServerTransport("atlantis-mcp", logs), options, logs,
                                                        new ServiceProviders(session));
        await server.RunAsync();
        return 0;
    }

    /// <summary>The one service the tools take: the session (M2's seam formalizes this).</summary>
    private sealed class ServiceProviders(RemoteSession session) : IServiceProvider
    {
        public object? GetService(Type serviceType) => serviceType == typeof(RemoteSession) ? session : null;
    }
}

/// <summary>Logging to stderr only -- stdout carries MCP and must stay clean. The minimal factory
/// hand-rolled because Microsoft.Extensions.Logging's concrete package is not a dependency (J1's set
/// stays one package).</summary>
internal sealed class StderrLoggerFactory : ILoggerFactory
{
    public ILogger CreateLogger(string categoryName) => new StderrLogger(categoryName);

    public void Dispose()
    {
    }

    public void AddProvider(ILoggerProvider provider)
    {
    }

    private sealed class StderrLogger(string category) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel >= LogLevel.Warning;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception,
                                Func<TState, Exception?, string> formatter)
        {
            if (!IsEnabled(logLevel)) return;
            Console.Error.WriteLine("atlantis-mcp: " + category + ": " + formatter(state, exception) +
                                     (exception is null ? "" : " (" + exception.Message + ")"));
        }
    }
}
