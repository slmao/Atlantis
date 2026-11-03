using System;
using System.IO;
using Atlantis.Gameplay.Remote;
using ModelContextProtocol.Protocol;

namespace Atlantis.Mcp;

internal sealed partial class ToolRegistry
{
    private static void AddCapture(CallToolResult forwarded, JsonValue result)
    {
        if (result.Find("ok")?.Find("image")?.Find("path") is not { IsString: true } path)
        {
            forwarded.IsError = true;
            forwarded.Content = [.. forwarded.Content, new TextContentBlock { Text = "ProtocolError: capture report has no PNG path" }];
            return;
        }
        try
        {
            // The Runtime's report is authoritative (the requested path can differ). Copy its
            // bytes into the MCP result; a later capture can then reuse/overwrite the same path.
            byte[] png = File.ReadAllBytes(path.AsString);
            if (png.Length == 0) throw new IOException("The capture PNG is empty");
            forwarded.Content = [.. forwarded.Content, ImageContentBlock.FromBytes(png, "image/png")];
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            forwarded.IsError = true;
            forwarded.Content = [.. forwarded.Content, new TextContentBlock { Text = "Reading capture PNG: " + e.Message }];
        }
    }
}
