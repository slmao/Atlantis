// Plan 0070 M1 (Spec 0070 R2): the tool registry -- the host's own seam. The
// tools are declared as McpServerTool instances (delegates, one per wire call
// or composition) and this class wires them into the McpServerOptions' list
// and call handlers by hand, which the SDK's DI setup would otherwise do; the
// SDK's own Hosting/DI packages are not needed and none is added (the pinned
// set stays the one allow-listed package, J1). M2 moves the tool table here
// wholesale; the seam keeps its one shape.
using System;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace Atlantis.Mcp;

/// <summary>The registered tools, wired into an McpServerOptions' tool handlers.</summary>
internal sealed class ToolRegistry
{
    private readonly ImmutableArray<McpServerTool> _tools;

    public ToolRegistry(ImmutableArray<McpServerTool> tools) => _tools = tools;

    /// <summary>Sets the list and call handlers of <paramref name="options"/> over the tools.</summary>
    public void Wire(McpServerOptions options)
    {
        options.Handlers = new McpServerHandlers
        {
            ListToolsHandler = (request, token) =>
            {
                ListToolsResult result = new()
                {
                    Tools = _tools.Select(t => t.ProtocolTool).ToImmutableArray(),
                };
                return ValueTask.FromResult(result);
            },
            CallToolHandler = (request, token) => Invoke(request, token),
        };
    }

    private async ValueTask<CallToolResult> Invoke(RequestContext<CallToolRequestParams> request, CancellationToken token)
    {
        string name = request.Params?.Name ?? "";
        McpServerTool? tool = _tools.FirstOrDefault(t => t.ProtocolTool.Name == name);
        if (tool is null)
        {
            return new CallToolResult
            {
                IsError = true,
                Content = [new TextContentBlock { Text = "UnknownTool: no tool is named '" + name + "'" }],
            };
        }
        return await tool.InvokeAsync(request, token);
    }
}
